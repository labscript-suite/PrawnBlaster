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
#include "pseudoclock.pio.h"

#define DEBUG 1

// Can't seem to have this be used to define array size even though it's a constant
const unsigned int max_instructions = 30000;
// max_instructions*2 + 2
unsigned int instructions[60002];
unsigned int waits[4];

char readstring[256] = "";
// This contains the number of clock cycles for a half period, which is currently 6 (there are 6 ASM instructions)
const unsigned int non_loop_path_length = 6;

const uint OUT_PIN = 25;
const uint IN_PIN = 0;

PIO pio;
uint sm;

// STATUS flag
int status;
#define STOPPED 0
#define RUNNING 1
#define ABORTING 2
#define ABORTED 3

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
        for (int i = 0; i < (max_instructions * 2 + 2); i += 2)
        {
            if (instructions[i] == 0 && instructions[i + 1] == 0)
            {
                instructions_to_send = i + 2;
                break;
            }
            else if (instructions[i] == 0)
            {
                wait_count += 1;
            }
        }
        if (DEBUG)
        {
            printf("Will send %d instructions\n", instructions_to_send);
        }

        if (hwstart)
        {
            // send initial wait command
            pio_sm_put_blocking(pio, sm, 0);
        }

        // send instructions
        for (int i = 0; i < instructions_to_send; i++)
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
                    waits[i] = wait*2; 
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

void loop()
{
    // fgets(readstring, 255, stdin);
    readline();

    // Check if the state machine has sent us new info
    check_status();

    if (strncmp(readstring, "hello", 5) == 0)
    {
        printf("hello\n");
    }
    else if (strncmp(readstring, "status", 6) == 0)
    {
        printf("%d\n", status);
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
    else if (strncmp(readstring, "readwaits", 9) == 0)
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

    memset(readstring, 0, 256 * (sizeof readstring[0]));
}

int main()
{
    stdio_init_all();

    multicore_launch_core1(core1_entry);
    multicore_fifo_pop_blocking();

    while (true)
    {
        loop();
    }
    return 0;
}
