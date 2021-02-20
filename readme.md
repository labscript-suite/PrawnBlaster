# PrawnBlaster

The PrawnBlaster firmware turns the $5 Raspberry Pi Pico microcontroller board into a [_labscript suite_](https://github.com/labscript-suite) pseudoclock device.

**Current Development Status**: Beta. The firmware has not been well tested. There may be timing errors, errors that appear when running for long periods of time, and/or missing or undocumented features. **There is currently no labscript device class.**

## What is a pseudoclock device?
A pseudoclock is a device that can be programmed to output a variable frequency clock.
The entire sequence of clock pulses is programmed into internal memory (over serial) and then executed on command (in order to achieve precise timing).
In the case of the PrawnBlaster, you can program in a series of instructions containing the period of clock ticks and the number of times you would like that clock tick to repeat before moving to the next instruction.

## Features/Specifications

* Only costs $5 (a tenth of the cost of the [PineBlaster](https://github.com/labscript-suite/pineblaster)).
* Support for up to 60000 instructions (4 times the PineBlaster).
* Each instruction can be repeated up to 4294967295 times (2^32-1).
* Minimum pulse half-period 60ns (if internal clock running at 100MHz).
* Maximum pulse half-period ~42.9 seconds (if internal clock running at 100MHz).
* Half-period resolution (between the minimum and maximum pulse half-period) of 10ns (if internal clock running at 100MHz).
* Support for initial hardware trigger (can be used as a secondary pseudoclock in the labscript-suite).
* Support for up to 100 retriggers mid-execution (labscript-suite waits).
* Support for timeouts on those waits (with maximum length 42.9 seconds).
* Ability to internally monitor the length of those waits and report them over the serial connection at the end of the instruction execution.
* Support for indefinite waits until retrigger (Note: the PrawnBlaster will not report the length of indefinite waits).
* Support for referencing to an external clock source to synchronise with other devices (may be limited to 50MHz - this has not yet been tested - which would double all of the timing specifications above).

Note 1: The half-period is the time a clock pulse stays high. All clock pulses produced by the PrawnBlaster have a 50-50 duty cycle.

Note 2: The internal clock can be configured to run at up to 133 MHz. We set it to a default of 100 MHz in order to produce nice round numbers.
You can increase the clock frequency at runtime (via a serial command) which scales the timing specifications accordingly.

## How to flash the firmware
Download the latest [prawnblaster.uf2 file](https://github.com/labscript-suite/PrawnBlaster/blob/master/build/prawnblaster/prawnblaster.uf2).
On your Raspberry Pi Pico, hold down the "bootsel" button while plugging the Pico into USB port on a PC (that must already be turned on).
The Pico should mount as a mass storage device (if it doesn't, try again or consult the Pico documentation).
Drag and drop the `.uf2` file into the mounted mass storage device.
The mass storage device should unmount after the copy completes. Your Pico is now running the PrawnBlaster firmware!

## PrawnBlaster pinout

* Clock output: GPIO 15 (configurable via serial command)
* Trigger input: GPIO 0 (configurable via serial command)
* External clock reference input: GPIO 20
* Debug clock output (can be fed into GPIO20 to test external clocking): GPIO 21 (48 MHz)

Note: Don't forget to connect the ground of your signal cables to one of the ground pins of the Pico!

## Communicating with the Pico
Communication is done over a USB serial connection. 
You should be able to use any terminal software of programming language to communicate. 
As an example, here is how you can launch a basic terminal with the Python PySerial package (assuming your Pico is on `COM4`):

```
python -m serial.tools.miniterm --eol=CRLF COM4 115200
```

Note the baudrate of `152000` and the requirement that commands be terminated with `\r\n` (CRLF).

**Communication during buffered execution should be fine (pending the results of testing detailed in [issue #4](https://github.com/labscript-suite/PrawnBlaster/issues/4)) as serial communication is handled by a separate core.**

## Supported serial commands.
Note: the commands are only read if terminated with `\r\n`.

* `status`: Responds with a string containing the PrawnBlaster status in the format `run-status:<int> clock-status:<int>` where the `run-status` integer is `0=manual-mode, 1=buffered-execution, 2=currently aborting buffered execution, 3=last buffered-execution aborted`. `clock-status` is either 0 (for internal clock) or 1 (for external clock).
* `getfreqs`: Responds with a multi-line string containing the current operating frequencies of various clocks (you will be most interested in `pll_sys` and `clk_sys`). Multiline string ends with `ok\n`.
* `abort`: Prematurely ends buffered-execution.
* `setclock <mode:int> <freq:int> <vcofreq:int> <plldiv1:int> <plldiv2:int>`: Reconfigures the internal clock. See below for more details.
* `getwait <wait:int>`: Returns an integer related to the length of wait number `wait`. `wait` starts at `0`. The length of the wait (in seconds) can be calculated by subtracting the returned value from the relevant wait timeout and dividing the result by the clock frequency (by default 100 MHz). A returned value of 0 means the wait timed out. There may be more waits available than were in your latest program. If you had `N` waits, query the first `N` values (starting from 0). Note that wait lengths and only accurate to +/- 1 clock cycle as the detection loop length is 2 clock cycles. Indefinite waits should report as `0` (assuming that the trigger pulse length is sufficient, see the FAQ below).
* `start`: Immediately triggers the execution of the instruction set.
* `hwstart`: Triggers the execution of the instruction set, but only after first detecting logical high on the trigger input (GPIO 0).
* `set <addr:int> <half-period:int> <reps:int>`: Sets the values of instruction number `addr`. `addr` starts at `0`. `half-period` is specified in clock cycles and must be at least `6` (and less than 2^32) for a normal instruction. `reps` should be `1` or more (and less than 2^32) for a normal instruction and indicates how many times the pulse should repeat. Special instructions can be specified with `reps=0`. A stop (end execution) instruction is specified by setting both `reps` and `half-period` to `0`. A wait instruction is specified by `reps=0` and `half-period=<wait timeout in clock cycles>`. Two waits in a row (sequential PrawnBlaster instructions) will trigger an indefinite wait should the first timeout expire (the second wait timeout is ignored and the length of this wait is not logged). See below (FAQ) for details on the requirements for trigger pulse lengths.
* `get <addr:int>`: Gets the half-period and reps of the instruction at `addr`. Return values are integers, separated by a space, in the same format as `set`.
* `go high`: Forces the GPIO output high (useful for debugging).
* `go low`: Forces the GPIO output low (useful for debugging).
* `setinpin <pin:int>`: Configures which GPIO to use for trigger input (defaults to GPIO 0). Should be between 0 and 19 inclusive.
* `setoutpin <pin:int>`: Configures which GPIO to use for pseudoclock output (defaults to GPIO 15). Should be between 0 and 19 inclusive or 25 (for the LED - useful for debugging without an oscilloscope).
* `debug <state:str>`: Turns on extra debug messages printed over serial. `state` should be `on` or `off` (no string quotes required).

## Reconfiguring the internal clock.
The clock frequency (and even source) can be reconfigured at runtime (it is initially set to 100 MHz on every boot).
To do this, you send the command `setclock <mode:int> <freq:int> <vcofreq:int> <plldiv1:int> <plldiv2:int>`, where the parameters are:

* mode: `0` means use the internal reference source and PLL. `1` means use an external source (bypassing the PLL) fed in on GPIO 20. `2` means use an external source (bypassing the PLL fed in on GPIO 22).
* freq: The clock frequency, in Hz (max 133000000).
* vcofreq: The clock frequency (in Hz) the internal VCO (in the PLL) should run at. Only relevant if `mode=0`. Must be between 400000000 and 1600000000. Must also satisfy the relationship `freq=vcofreq/plldiv1/plldiv2` (this is not internally verified).
* plldiv1: The first PLL divider. Must be between 1 and 7 (inclusive). Should be greater than plldiv2 to reduce power usage.
* plldiv2 The second PLL divider. Must be between 1 and 7 (inclusive). Should be lower than plldiv1 to reduce power usage.

For example, to set the internal frequency to 125 MHz, you would send:

```
setclock 0 125000000 1500000000 6 2
```

To set the internal frequency back to 100 MHz, you would send:

```
setclock 0 100000000 1200000000 6 2
```

To use a 50 MHz external reference (on GPIO 20) you would send:

```
setclock 1 50000000 0 0 0
```

Note: When configured to use an external reference, the board will lock up if the reference is interrupted. 
In this case you will need to power cycle the Pico. 
See [issue #1](https://github.com/labscript-suite/PrawnBlaster/issues/1).


## FAQ:

### Why is it called a "PrawnBlaster"?
The PrawnBlaster is named after the Australian $5 note, which is colloquially known as a "Prawn". 
This follows the tradition started by the PineBlaster which was named after the Australian $50 note (colloquially known as the "Pineapple").

### What are the trigger pulse requirements
The initial start trigger (if using `hwstart`) and standard waits should only require a trigger pulse that is 3 clock cycles long (there is a 2 clock cycle buffer in the Pico GPIO design to reject spurious pulses).

Note that using indefinite waits requires that your trigger pulse is at least 15 clock cycles long and that it does not go high until after the previous instruction has completed.
If this requirement is not met, you may find that your first wait (in the indefinite wait) reports a wait length but the second (indefinite) wait is not immediately processed until a subsequent trigger pulse. This is due to the architecture of how indefinite waits are defined (as two sequential waits).
