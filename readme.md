# PrawnBlaster

The PrawnBlaster firmware turns the $5 Raspberry Pi Pico microcontroller board into a [_labscript suite_](https://github.com/labscript-suite) pseudoclock device.

**Current Development Status**: Beta. The firmware has not been well tested. There may be timing errors, errors that appear when running for long periods of time, and/or missing or undocumented features. **There is currently no labscript device class.**

## What is a pseudoclock device?
A pseudoclock is a device that can be programmed to output a variable frequency clock.
The entire sequence of clock pulses is programmed into internal memory (over serial) and then executed on command (in order to achieve precise timing).
In the case of the PrawnBlaster, you can program in a series of instructions containing the period of clock ticks and the number of times you would like that clock tick to repeat before moving to the next instruction.

## Features/Specifications

* Only costs $5 (a tenth of the cost of the [PineBlaster](https://github.com/labscript-suite/pineblaster)).
* Support for up to 60000 instructions (4 times the PineBlaster) spread evenly across 1 to 4 independent pseudoclocks (also 4 times the PineBlaster).
* Each instruction can be repeated up to 4294967295 times (2^32-1).
* Minimum pulse half-period 50ns (if internal clock running at 100MHz).
* Maximum pulse half-period ~42.9 seconds (if internal clock running at 100MHz).
* Half-period resolution (between the minimum and maximum pulse half-period) of 10ns (if internal clock running at 100MHz).
* Support for initial hardware trigger (can be used as a secondary pseudoclock in the labscript-suite).
* Support for up to 100 retriggers mid-execution (labscript-suite waits) per independent pseudoclock.
* Support for timeouts on those waits (with maximum length 42.9 seconds).
* Ability to internally monitor the length of those waits and report them over the serial connection at the end of the instruction execution.
* Support for indefinite waits until retrigger (Note: the PrawnBlaster will not report the length of indefinite waits).
* Support for referencing to an external clock source to synchronise with other devices (officially limited to 50MHz on the pico but testing has shown it works up to 125MHz, see [#6](https://github.com/labscript-suite/PrawnBlaster/issues/6)).

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

* Pseudoclock 1 output: GPIO 9 (configurable via serial command)
* Pseudoclock 2 output: GPIO 11 (configurable via serial command)
* Pseudoclock 3 output: GPIO 13 (configurable via serial command)
* Pseudoclock 4 output: GPIO 15 (configurable via serial command)
* Pseudoclock 1 trigger input: GPIO 0 (configurable via serial command)
* Pseudoclock 2 trigger input: GPIO 2 (configurable via serial command)
* Pseudoclock 3 trigger input: GPIO 4 (configurable via serial command)
* Pseudoclock 4 trigger input: GPIO 6 (configurable via serial command)
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

**Communication during buffered execution should be fine (pending the results of testing detailed in [issue #7](https://github.com/labscript-suite/PrawnBlaster/issues/7)) as serial communication is handled by a separate core.**

## Supported serial commands.
Note: the commands are only read if terminated with `\r\n`.

* `status`: Responds with a string containing the PrawnBlaster status in the format `run-status:<int> clock-status:<int>` where the `run-status` integer is `0=manual-mode, 1=transitioning to buffered-execution, 2=buffered-execution, 3=abort requested, 4=currently aborting buffered execution, 5=last buffered-execution aborted, 6=transitioning to manual-mode`. `clock-status` is either 0 (for internal clock) or 1 (for external clock).
* `getfreqs`: Responds with a multi-line string containing the current operating frequencies of various clocks (you will be most interested in `pll_sys` and `clk_sys`). Multiline string ends with `ok\n`.
* `abort`: Prematurely ends buffered-execution.
* `setclock <mode:int> <freq:int>`: Reconfigures the clock source. See below for more details.
* `setnumpseudoclocks <number:int>`: Set the number of independent pseudoclocks. Must be between 1 and 4 (inclusive). Default at boot is 1. Configuring a number higher than one reduces the number of available instructions per pseudoclock by that factor. E.g. 2 pseudoclocks have 30,000 instructions each. 3 pseudoclocks have 20,000 instructions each. 4 pseudoclocks have 15,000 instructions each.
* `getwait <pseudoclock:int> <wait:int>`: Returns an integer related to the length of wait number `wait` for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). `wait` starts at `0`. The length of the wait (in seconds) can be calculated by subtracting the returned value from the relevant wait timeout and dividing the result by the clock frequency (by default 100 MHz). A returned value of `4294967295` (`2^32-1`) means the wait timed out. There may be more waits available than were in your latest program. If you had `N` waits, query the first `N` values (starting from 0). Note that wait lengths and only accurate to +/- 1 clock cycle as the detection loop length is 2 clock cycles. Indefinite waits should report as `4294967295` (assuming that the trigger pulse length is sufficient, see the FAQ below).
* `start`: Immediately triggers the execution of the instruction set.
* `hwstart`: Triggers the execution of the instruction set(s), but only after first detecting logical high on the trigger input(s).
* `set <pseudoclock:int> <addr:int> <half-period:int> <reps:int>`: Sets the values of instruction number `addr` for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). `addr` starts at `0`. `half-period` is specified in clock cycles and must be at least `5` (and less than 2^32) for a normal instruction. `reps` should be `1` or more (and less than 2^32) for a normal instruction and indicates how many times the pulse should repeat. Special instructions can be specified with `reps=0`. A stop (end execution) instruction is specified by setting both `reps` and `half-period` to `0`. A wait instruction is specified by `reps=0` and `half-period=<wait timeout in clock cycles>` where the wait-timeout/half-period must be at least 6 clock cycles. Two waits in a row (sequential PrawnBlaster instructions) will trigger an indefinite wait should the first timeout expire (the second wait timeout is ignored and the length of this wait is not logged). See below (FAQ) for details on the requirements for trigger pulse lengths.
* `get <pseudoclock:int> <addr:int>`: Gets the half-period and reps of the instruction at `addr` for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). Return values are integers, separated by a space, in the same format as `set`.
* `go high <pseudoclock:int>`: Forces the GPIO output high for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). This is useful for debugging.
* `go low <pseudoclock:int>`: Forces the GPIO output low for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). This is useful for debugging.
* `setinpin <pseudoclock:int> <pin:int>`: Configures which GPIO to use for the pseudoclock `pseudoclock` trigger input (pseudoclock is zero indexed). Defaults to GPIO 0, 2, 4, and 6 for pseudoclocks 0, 1, 2 and 3 respectively. Should be between 0 and 19 inclusive.
* `setoutpin <pseudoclock:int> <pin:int>`: Configures which GPIO to use for the pseudoclock `pseudoclock` output (pseudoclock is zero indexed). Defaults to GPIO 9, 11, 13, and 15 for pseudoclocks 0, 1, 2, and 3 respectively. Should be between 0 and 19 inclusive or 25 (for the LED - useful for debugging without an oscilloscope).
* `debug <state:str>`: Turns on extra debug messages printed over serial. `state` should be `on` or `off` (no string quotes required).

## Reconfiguring the internal clock.
The clock frequency (and even source) can be reconfigured at runtime (it is initially set to 100 MHz on every boot).
To do this, you send the command `setclock <mode:int> <freq:int>`, where the parameters are:

* mode: `0` means use the internal reference source and PLL. `1` means use an external source (bypassing the PLL) fed in on GPIO 20. `2` means use an external source (bypassing the PLL fed in on GPIO 22).
* freq: The clock frequency, in Hz (max 133000000).

For example, to set the internal frequency to 125 MHz, you would send:

```
setclock 0 125000000
```

To set the internal frequency back to 100 MHz, you would send:

```
setclock 0 100000000
```

To use a 50 MHz external reference (on GPIO 20) you would send:

```
setclock 1 50000000
```

Note: When configured to use an external reference, the board will revert to the internal clock (at 100 MHz) if the external reference is interrupted.

### External reference requirements
If configured to use an external source (clock mode `1` or `2`, see above), the source must be a 0-3.3V CMOS signal at the frequency you wish to run at.
This directly clocks the cores running the PrawnBlaster firmware, so minimum pulse lengths are directly related to the reference frequency.
An external clock of 50MHz means a minimum half-period of `5/50MHz=100ns`.
An external clock of 100MHz means a minimum half-period of `5/100MHz=50ns`.

Note: Officially, the documentation for the Pico says external clock sources can only be up to 50MHz. We have successfully tested up to 125MHz (see [#6](https://github.com/labscript-suite/PrawnBlaster/issues/6)).
We recommend you personally verify the output of the PrawnBlaster is as expected if running from an external reference above 50MHz.

## FAQ:

### Why is it called a "PrawnBlaster"?
The PrawnBlaster is named after the Australian $5 note, which is colloquially known as a "Prawn". 
This follows the tradition started by the PineBlaster which was named after the Australian $50 note (colloquially known as the "Pineapple").

### What are the trigger pulse requirements
The initial start trigger (if using `hwstart`) and standard waits should only require a trigger pulse that is 4 clock cycles long (there is a 2 clock cycle buffer in the Pico GPIO design to reject spurious pulses).

Note that using indefinite waits requires that your trigger pulse is at least 12 clock cycles long and that it does not go high until 4 clock cycles after the previous instruction has completed.
If this requirement is not met, you may find that your first wait (in the indefinite wait) reports a wait length and/or the second (indefinite) wait is not immediately processed until a subsequent trigger pulse. This is due to the architecture of how indefinite waits are defined (as two sequential waits).
