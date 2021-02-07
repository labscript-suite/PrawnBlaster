/*
#######################################################################
#                                                                     #
# prawnblaster.c                                                      #
#                                                                     #
# Copyright 2021, Philip Starkey                                      #
#                                                                     #
# Serial communication code based on the PineBlaster                  #
#   https://github.com/labscript-suite/pineblaster                    #
#   Copyright 2013, Christopher Billington                            #
#                                                                     #
# This file is used to flash a Raspberry Pi Pico microcontroller      #
# prototyping board to create a PrawnBlaster (see readme.txt and      #
# http://hardware.labscriptsuite.org).                                #
# This file is licensed under the Simplified BSD License.             #
# See the license.txt file for the full license.                      #
#                                                                     #
#######################################################################
*/

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/clocks.h"

#include "pseudoclock.pio.h"

int DEBUG;

// Can't seem to have this be used to define array size even though it's a constant
const unsigned int max_instructions = 30000;
// max_instructions*2 + 2
unsigned int instructions[60002];
unsigned int waits[4];

char readstring[256] = "";
// This contains the number of clock cycles for a half period, which is currently 6 (there are 6 ASM instructions)
const unsigned int non_loop_path_length = 6;

uint OUT_PIN = 15;
uint IN_PIN = 0;

PIO pio;
uint sm;

// STATUS flag
int status;
#define STOPPED 0
#define RUNNING 1
#define ABORTING 2
#define ABORTED 3

// Clock status flag
int clock_status;
#define INTERNAL 0
#define EXTERNAL 1

// Frequency variables
volatile bool resus_complete;
volatile bool resus_expected;
unsigned int _src;  // 0 = internal, 1=GPIO pin 20, 2=GPIO pin 22
unsigned int _freq; // in Hz
unsigned int _vcofreq;
unsigned int _div1;
unsigned int _div2;

void core1_entry()
{
    // PIO initialisation
    pio = pio0;
    uint offset = pio_add_program(pio, &pseudoclock_program);

    // announce we are ready
    multicore_fifo_push_blocking(0);

    while (true)
    {
        // wait for message from main core
        uint32_t hwstart = multicore_fifo_pop_blocking();

        // Configure PIO Statemachine
        sm = pio_claim_unused_sm(pio, true);
        pio_pseudoclock_init(pio, sm, offset, OUT_PIN, IN_PIN);

        // Find the number of instructions to send
        int instructions_to_send = 0;
        int wait_count = 0;
        bool previous_instruction_was_wait = false;
        for (int i = 0; i < (max_instructions * 2 + 2); i += 2)
        {
            if (instructions[i] == 0 && instructions[i + 1] == 0)
            {
                instructions_to_send = i + 2;
                break;
            }
            else if (instructions[i] == 0)
            {
                // Only count the first wait in a set of sequential waits
                if (!previous_instruction_was_wait)
                {
                    wait_count += 1;
                }
                previous_instruction_was_wait = true;
            }
            else
            {
                previous_instruction_was_wait = false;
            }
        }
        if (DEBUG)
        {
            printf("Will send %d instructions\n", instructions_to_send);
        }

        int initial_inst_offset = 2;
        if (hwstart)
        {
            // send initial wait command
            pio_sm_put_blocking(pio, sm, 0);
            pio_sm_put_blocking(pio, sm, 1); // This is ignored by the PIO code
            // Send the first two instructions to fill the FIFO
            pio_sm_put_blocking(pio, sm, instructions[0]);
            pio_sm_put_blocking(pio, sm, instructions[1]);
        }
        else
        {
            pio_sm_put_blocking(pio, sm, instructions[0]);
            pio_sm_put_blocking(pio, sm, instructions[1]);
            pio_sm_put_blocking(pio, sm, instructions[2]);
            pio_sm_put_blocking(pio, sm, instructions[3]);
            initial_inst_offset += 2;
        }
        
        // start the PIO
        pio_sm_set_enabled(pio, sm, true);
        
        // send remaining instructions
        for (int i = initial_inst_offset; i < instructions_to_send; i++)
        {
            pio_sm_put_blocking(pio, sm, instructions[i]);
        }

        // Now read the correct number of waits from the buffer
        for (int i = 0; i < wait_count; i++)
        {
            // don't block in case we abort between putting the last instructions in the FIFO and the
            // program actually ending.
            // So wait until there is something in the FIFO before entering the blocking call
            while (pio_sm_get_rx_fifo_level(pio, sm) == 0 && status != ABORTING)
            {
                tight_loop_contents();
            }

            if (status != ABORTING)
            {
                unsigned int wait = pio_sm_get_blocking(pio, sm);

                // Check for timeout wait.
                // This integer equals 2^32-1 - aka the biggest number and what happens when you subtract one from 0 (it wraps around)
                // Note that we do not need to
                if (wait == 4294967295)
                {
                    waits[i] = 0; // decrement happens regardless of jump condition in PIO program.
                }
                else
                {
                    // This does not need a -1 offset like above since we jumped on pin change, and did not unecessarily hit an extra
                    // jump+decrement PIO instruction. However, we do need to double it to put it back in terms of the clock frequency
                    // since the wait loop is 2 PIO instructions (the wait length was halved when written using the "set" serial command)
                    waits[i] = wait * 2;
                }
            }
        }

        // Check if we aborted in core 0 (and drained the FIFO)
        if (status != ABORTING)
        {
            // Get completed message
            pio_sm_get_blocking(pio, sm);

            if (DEBUG)
            {
                printf("Pseudoclock program complete\n");
            }
        }
        else
        {
            if (DEBUG)
            {
                printf("Draining wait FIFO\n");
            }
            // drain rx fifo
            while (pio_sm_get_rx_fifo_level(pio, sm) > 0)
            {
                pio_sm_get(pio, sm);
            }
            if (DEBUG)
            {
                printf("Pseudoclock program aborted\n");
            }
        }


        // release the state machine
        pio_sm_unclaim(pio, sm);

        if (DEBUG)
        {
            printf("Draining TX FIFO\n");
        }
        // drain the tx FIFO to be safe
        pio_sm_drain_tx_fifo(pio, sm);

        // Tell main core we are done
        multicore_fifo_push_blocking(1);
        if (DEBUG)
        {
            printf("Core1 loop ended\n");
        }
    }
}

void readline()
{
    int i = 0;
    char c;
    int crfound = 0;
    while (true)
    {
        char c = fgetc(stdin);
        if (c == '\r')
        {
            crfound = 1;
        }
        else if (c == '\n')
        {
            if (crfound == 1)
            {
                readstring[i] = '\0';
                return;
            }
            else
            {
                readstring[i] = '\n';
                i++;
            }
        }
        else if (crfound)
        {
            crfound = 0;
            readstring[i] = '\r';
            i++;
            readstring[i] = c;
            i++;
        }
        else
        {
            readstring[i] = c;
            i++;
        }
    }
}

void configure_gpio()
{
    // initialise output pin. Needs to be done after state machine has run
    gpio_init(OUT_PIN);
    gpio_set_dir(OUT_PIN, GPIO_OUT);
}

void check_status()
{
    if (multicore_fifo_rvalid())
    {
        if (DEBUG)
        {
            printf("Reading from core 1\n");
        }
        multicore_fifo_pop_blocking();
        if (DEBUG)
        {
            printf("Core 1 read finished\n");
        }

        if (status == ABORTING)
        {
            status = ABORTED;
        }
        else
        {
            status = STOPPED;
        }
    }
}

void measure_freqs(void)
{
    // From https://github.com/raspberrypi/pico-examples under BSD-3-Clause License
    uint f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
    uint f_pll_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY);
    uint f_rosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    uint f_clk_peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    uint f_clk_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    uint f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
    uint f_clk_rtc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_RTC);

    printf("pll_sys = %dkHz\n", f_pll_sys);
    printf("pll_usb = %dkHz\n", f_pll_usb);
    printf("rosc = %dkHz\n", f_rosc);
    printf("clk_sys = %dkHz\n", f_clk_sys);
    printf("clk_peri = %dkHz\n", f_clk_peri);
    printf("clk_usb = %dkHz\n", f_clk_usb);
    printf("clk_adc = %dkHz\n", f_clk_adc);
    printf("clk_rtc = %dkHz\n", f_clk_rtc);
}

void resus_callback(void)
{
    // If we were not expecting a resus, switch back to internal clock
    if (!resus_expected)
    {
        // kill external clock pin
        gpio_set_function((_src == 2 ? 22 : 20), GPIO_FUNC_NULL);

        // reinitialise pll
        pll_init(pll_sys, 1, 1200, 6, 2);

        // Configure internal clock
        clock_configure(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                        100 * MHZ,
                        100 * MHZ);

        // update clock status
        clock_status = INTERNAL;
    }
    else if (_src > 0)
    {
        clock_configure_gpin(clk_sys, (_src == 2 ? 22 : 20), _freq, _freq);
        // update clock status
        clock_status = EXTERNAL;
    }
    else
    {
        // Reconfigure PLL sys back to the default state of 1500 / 6 / 2 = 125MHz
        // pll_init(pll_sys, 1, 1200 * MHZ, 6, 2);
        pll_init(pll_sys, 1, _vcofreq, _div1, _div2);

        // CLK SYS = PLL SYS (125MHz) / 1 = 125MHz
        clock_configure(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                        _freq,
                        _freq);

        // update clock status
        clock_status = INTERNAL;
    }

    // Reconfigure uart as clocks have changed
    stdio_init_all();
    if (DEBUG)
    {
        printf("Resus event fired\n");
    }

    // Wait for uart output to finish
    uart_default_tx_wait_blocking();

    resus_complete = true;
}

void loop()
{
    // fgets(readstring, 255, stdin);
    readline();

    // Check if the state machine has sent us new info
    check_status();

    if (strncmp(readstring, "status", 6) == 0)
    {
        printf("run-status:%d clock-status:%d\n", status, clock_status);
    }
    else if (strncmp(readstring, "debug on", 8) == 0)
    {
        DEBUG = 1;
        printf("ok\n");
    }
    else if (strncmp(readstring, "debug off", 9) == 0)
    {
        DEBUG = 0;
        printf("ok\n");
    }
    else if (strncmp(readstring, "getfreqs", 8) == 0)
    {
        measure_freqs();
        printf("ok\n");
    }
    else if (strncmp(readstring, "abort", 5) == 0)
    {
        if (status != RUNNING)
        {
            printf("Cannot abort in manual mode.");
        }
        else
        {
            // force output low first, this should take control from the state machine
            // and prevent it from changing the output pin state erroneously as we drain the fifo
            status = ABORTING;
            configure_gpio();
            gpio_put(OUT_PIN, 0);
            // This might be thread unsafe...oh well!
            pio_sm_drain_tx_fifo(pio, sm);
            // Should be done!
            printf("ok\n");
        }
    }
    // Prevent manual mode commands from running during buffered execution
    else if (status == RUNNING || status == ABORTING)
    {
        printf("Cannot execute command %s during buffered execution. Check status first and wait for it to return 0.\n", readstring);
    }
    else if (strncmp(readstring, "setinpin", 8) == 0)
    {
        unsigned int pin_no;
        int parsed = sscanf(readstring, "%*s %u", &pin_no);
        if (parsed < 1)
        {
            printf("invalid request\n");
        }
        else if (pin_no == OUT_PIN)
        {
            printf("IN pin cannot be the same as the OUT pin");
        }
        else if (pin_no < 0 || pin_no > 19)
        {
            printf("IN pin must be between 0 and 19 (inclusive)\n");
        }
        else
        {
            IN_PIN = pin_no;
            printf("ok\n");
        }
    }
    else if (strncmp(readstring, "setoutpin", 9) == 0)
    {
        unsigned int pin_no;
        int parsed = sscanf(readstring, "%*s %u", &pin_no);
        if (parsed < 1)
        {
            printf("invalid request\n");
        }
        else if (pin_no == IN_PIN)
        {
            printf("OUT pin cannot be the same as the IN pin");
        }
        else if (pin_no != 25 && (pin_no < 0 || pin_no > 19))
        {
            printf("OUT pin must be between 0 and 19 (inclusive) or 25 (LED for debugging)\n");
        }
        else
        {
            OUT_PIN = pin_no;
            printf("ok\n");
        }
    }
    else if (strncmp(readstring, "setclock", 8) == 0)
    {
        unsigned int src;       // 0 = internal, 1=GPIO pin 20, 2=GPIO pin 22
        unsigned int freq;      // in Hz (up to 133 MHz)
        unsigned int vcofreq;   // in Hz (between 400MHz and 1600MHz)
        unsigned int div1;      // First PLL divider (between 1-7)
        unsigned int div2;      // Second PLL divider (between 1-7)
                                // Note: freq should equal vcofreq / div1 / div2
        int parsed = sscanf(readstring, "%*s %u %u %u %u %u", &src, &freq, &vcofreq, &div1, &div2);
        if (parsed < 5)
        {
            printf("invalid request\n");
        }
        else
        {
            if (DEBUG)
            {
                printf("Got request mode=%u, freq=%u MHz, vco_freq=%u MHz, div1=%u, div2=%u\n", src, freq / MHZ, vcofreq / MHZ, div1, div2);
            }
            if (src > 2)
            {
                printf("invalid request\n");
            }
            else
            {
                // Do validation checking on values provided
                if (freq > 133 * MHZ)
                {
                    printf("Invalid clock frequency specified\n");
                    return;
                }
                else if (src == 0 && (vcofreq < (400 * MHZ) || vcofreq > (1600 * MHZ)))
                {
                    printf("Invalid VCO frequency specified\n");
                    return;
                }
                else if (src == 0 && (div1 < 1 || div1 > 7))
                {
                    printf("Invalid POSTDIV1 PLL divider specified\n");
                    return;
                }
                else if (src == 0 && (div2 < 1 || div2 > 7))
                {
                    printf("Invalid POSTDIV2 PLL divider specified\n");
                    return;
                }

                unsigned int old_src = _src;
                _src = src;
                _freq = freq;
                _vcofreq = vcofreq;
                _div1 = div1;
                _div2 = div2;

                // reset seen resus flag
                resus_complete = false;
                resus_expected = true;

                if (old_src > 0)
                {
                    // cancel clock input on this pin to trigger resus (and thus reconfigure)
                    gpio_set_function((old_src == 2 ? 22 : 20), GPIO_FUNC_NULL);
                }
                else
                {
                    // Break PLL to trigger resus (and thus reconfigure)
                    pll_deinit(pll_sys);
                }

                // Wait for resus to complete
                while (!resus_complete);
                resus_expected = false;

                printf("ok\n");
            }
        }
    }
    else if (strncmp(readstring, "getwaits", 8) == 0)
    {
        // Note that these are not the lengths of the waits, but how many base (system) clock ticks were left
        // before timeout. 0 = timeout. a wait with a timeout of 8, and a value reported here as 2, means the
        // wait was 6 clock ticks long.
        printf("%u %u %u %u\n", waits[0], waits[1], waits[2], waits[3]);
    }
    else if (strncmp(readstring, "hwstart", 7) == 0)
    {
        // Force output low in case it was left high
        gpio_put(OUT_PIN, 0);
        // Notify state machine to start
        multicore_fifo_push_blocking(1);
        // update status
        status = RUNNING;
        printf("ok\n");
    }
    else if ((strncmp(readstring, "start", 5) == 0)) // || (strcmp(readstring, "") == 0))
    {
        // Force output low in case it was left high
        gpio_put(OUT_PIN, 0);
        // Notify state machine to start
        multicore_fifo_push_blocking(0);
        // update status
        status = RUNNING;
        printf("ok\n");
    }
    else if (strncmp(readstring, "set ", 4) == 0)
    {
        unsigned int addr;
        unsigned int half_period;
        unsigned int reps;
        int parsed = sscanf(readstring, "%*s %u %u %u", &addr, &half_period, &reps);
        if (parsed < 3)
        {
            printf("invalid request\n");
        }
        else if (addr >= max_instructions)
        {
            printf("invalid address\n");
        }
        else if (reps == 0)
        {
            // This indicates either a stop or a wait instruction
            instructions[2 * addr] = 0;
            if (half_period == 0)
            {
                // It's a stop instruction
                instructions[2 * addr + 1] = 0;
                printf("ok\n");
            }
            else if (half_period >= 2)
            {
                // It's a wait instruction:
                // The half period contains the number of ASM wait loops to wait for before continuing.
                // The wait loop conatins two ASM instructions, so we divide by 2 here.
                instructions[2 * addr + 1] = half_period / 2;
                printf("ok\n");
            }
            else
            {
                printf("invalid request\n");
            }
        }
        else if (half_period < (non_loop_path_length))
        {
            printf("half-period too short\n");
        }
        else if (reps < 1)
        {
            printf("reps must be at least one\n");
        }
        else
        {
            instructions[2 * addr] = reps;
            instructions[2 * addr + 1] = half_period - non_loop_path_length;
            printf("ok\n");
        }
    }
    else if (strncmp(readstring, "get ", 4) == 0)
    {
        unsigned int addr;
        int parsed = sscanf(readstring, "%*s %u", &addr);
        if (parsed < 1)
        {
            printf("invalid request\n");
        }
        else if (addr >= max_instructions)
        {
            printf("invalid address\n");
        }
        else
        {
            uint half_period = instructions[2 * addr + 1];
            uint reps = instructions[2 * addr];
            if (reps != 0)
            {
                half_period += non_loop_path_length;
            }
            else
            {
                half_period = half_period * 2;
            }
            printf("%u %u\n", half_period, reps);
        }
    }
    else if (strncmp(readstring, "go high", 7) == 0)
    {
        configure_gpio();
        gpio_put(OUT_PIN, 1);
        printf("ok\n");
    }
    else if (strncmp(readstring, "go low", 6) == 0)
    {
        configure_gpio();
        gpio_put(OUT_PIN, 0);
        printf("ok\n");
    }
    else
    {
        printf("invalid request: %s\n", readstring);
    }

    //memset(readstring, 0, 256 * (sizeof readstring[0]));
}

int main()
{
    DEBUG = 0;

    // configure resus callback that will reconfigure the clock for us
    // either when we change clock settings or when the clock fails (if external)
    clocks_enable_resus(&resus_callback);

    // initial clock configuration
    _src = 0;
    _freq = 100*MHZ;
    _vcofreq = 1200*MHZ;
    _div1 = 6;
    _div2 = 2;
    // reset seen resus flag
    resus_complete = false;
    resus_expected = true;
    pll_deinit(pll_sys);
    // Wait for resus to complete
    while (!resus_complete);
    resus_expected = false;

    // Temp output 48MHZ clock for debug
    clock_gpio_init(21, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_USB, 1);

    stdio_init_all();

    multicore_launch_core1(core1_entry);
    multicore_fifo_pop_blocking();

    while (true)
    {
        loop();
    }
    return 0;
}
