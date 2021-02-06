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

// Can't seem to have this be used to define array size even though it's a constant
const unsigned int max_instructions = 30000;
// max_instructions*2 + 2
unsigned int instructions[60002];

char readstring[256] = "";
int autostart;
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

void core1_entry()
{
    // Configure PIO Statemachine
    pio = pio0;
    uint offset = pio_add_program(pio, &pseudoclock_program);
    sm = pio_claim_unused_sm(pio, true);
    pio_pseudoclock_init(pio, sm, offset, OUT_PIN, IN_PIN);

    // announce we are ready
    multicore_fifo_push_blocking(0);

    while (true)
    {
        // wait for message from main core
        multicore_fifo_pop_blocking();

        // Find the number of instructions to send
        int instructions_to_send = 0;
        int wait_count = 0;
        int waits[4];
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
        printf("Will send %d instructions\n", instructions_to_send);
        for (int i = 0; i < instructions_to_send; i++)
        {
            pio_sm_put_blocking(pio, sm, instructions[i]);
        }
        // Now read the correct number of waits from the buffer
        for (int i = 0; i < wait_count; i++)
        {
            waits[i] = pio_sm_get_blocking(pio, sm);
        }
        // Get completed message
        pio_sm_get_blocking(pio, sm);

        printf("Pseudoclock program complete\n");

        // Tell main core we are done
        multicore_fifo_push_blocking(1);
        printf("Core1 loop ended\n");
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

void loop()
{
    // fgets(readstring, 255, stdin);
    readline();

    if (strncmp(readstring, "hello", 5) == 0)
    {
        printf("hello\n");
    }
    else if (strncmp(readstring, "status", 6) == 0)
    {
        if (multicore_fifo_rvalid()) {
            multicore_fifo_pop_blocking();
            status = STOPPED;
        }
        printf("%d\n", status);

    }
    else if (strncmp(readstring, "hwstart", 7) == 0)
    {
        autostart = 0;
        multicore_fifo_push_blocking(0);
        // update status
        status = RUNNING;
    }
    else if ((strncmp(readstring, "start", 5) == 0)) // || (strcmp(readstring, "") == 0))
    {
        autostart = 1;
        multicore_fifo_push_blocking(0);
        // update status
        status = RUNNING;
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
            printf("%u %u\n", half_period, reps);
        }
    }
    else if (strncmp(readstring, "go high", 7) == 0)
    {
        gpio_put(OUT_PIN, 1);
        printf("ok\n");
    }
    else if (strncmp(readstring, "go low", 6) == 0)
    {
        gpio_put(OUT_PIN, 0);
        printf("ok\n");
    }
    // else if (strncmp(readstring, "reset", 5) == 0)
    // {
    //     printf("ok\n");
    //     //asm volatile("j reset\n\t");
    // }
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

    // initialise output pin
    //gpio_init(OUT_PIN);
    //gpio_set_dir(OUT_PIN, GPIO_OUT);

    while (true)
    {
        loop();
    }
    return 0;
}
