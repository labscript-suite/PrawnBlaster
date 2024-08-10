# PrawnBlaster

The PrawnBlaster firmware turns the $5 Raspberry Pi Pico microcontroller board into a [_labscript suite_](https://github.com/labscript-suite) pseudoclock device.

The firmware is compatible with both the Pico 2 board (based on the RP2350 chip) and the original Pico board (based on the RP2040 chip).
**We recommend ensuring you have the [official Pico 2 board](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) with the RP2350 chip for best performance.**
The [original Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040 chip) supports half the number of instructions and has a slower maximum clock speed.

## What is a pseudoclock device?
A pseudoclock is a device that can be programmed to output a variable frequency clock.
The entire sequence of clock pulses is programmed into internal memory (over serial) and then executed on command (in order to achieve precise timing).
In the case of the PrawnBlaster, you can program in a series of instructions containing the period of clock ticks and the number of times you would like that clock tick to repeat before moving to the next instruction.

## Features/Specifications

* Only costs $5 (a tenth of the cost of the [PineBlaster](https://github.com/labscript-suite/pineblaster)).
* Support for up to 60000 instructions (8 times the PineBlaster) spread evenly across 1 to 4 independent pseudoclocks (also 4 times the PineBlaster).
* Each instruction can be repeated up to 4294967295 times (2^32-1).
* Minimum pulse half-period 50ns (if internal clock running at 100MHz).
* Maximum pulse half-period ~42.9 seconds (if internal clock running at 100MHz).
* Half-period resolution (between the minimum and maximum pulse half-period) of 10ns (if internal clock running at 100MHz).
* Support for initial hardware trigger (can be used as a secondary pseudoclock in the labscript-suite).
* Support for up to 100 retriggers mid-execution (labscript-suite waits) per independent pseudoclock.
* Support for timeouts on those waits (with maximum length 42.9 seconds).
* Ability to internally monitor the length of those waits and report them over the serial connection at the end of the instruction execution.
* Support for indefinite waits until retrigger (Note: the PrawnBlaster will not report the length of indefinite waits).
* Support for referencing to an external clock source to synchronise with other devices (officially limited to 50MHz on the Pico but testing has shown it works up to 133MHz, see [#6](https://github.com/labscript-suite/PrawnBlaster/issues/6)).

Note 1: The half-period is the time a clock pulse stays high. All clock pulses produced by the PrawnBlaster have a 50-50 duty cycle.

Note 2: The internal clock can be configured to run at up to 150 MHz (Pico 2 - RP2350) or 133 MHz (Pico - RP2040). We set it to a default of 100 MHz in order to produce nice round numbers.
You can increase the clock frequency at runtime (via a serial command) which scales the timing specifications accordingly.

Note 3: The original Pico (RP2040) only supports up to 30000 instructions (4 times the PineBlaster).

## How to flash the firmware
Download the latest prawnblaster.uf2 file: [Pico - RP2040](https://github.com/labscript-suite/PrawnBlaster/releases/latest/download/prawnblaster_rp2040.uf2), [Pico 2 - RP2350](https://github.com/labscript-suite/PrawnBlaster/releases/latest/download/prawnblaster_rp2350.uf2).
On your Raspberry Pi Pico, hold down the "bootsel" button while plugging the Pico into USB port on a PC (that must already be turned on).
The Pico should mount as a mass storage device (if it doesn't, try again or consult the Pico documentation).
Drag and drop the `.uf2` file into the mounted mass storage device.
The mass storage device should unmount after the copy completes. Your Pico is now running the PrawnBlaster firmware!

## PrawnBlaster pinout

* Pseudoclock 0 output: GPIO 9 (configurable via serial command)
* Pseudoclock 1 output: GPIO 11 (configurable via serial command)
* Pseudoclock 2 output: GPIO 13 (configurable via serial command)
* Pseudoclock 3 output: GPIO 15 (configurable via serial command)
* Pseudoclock 0 trigger input: GPIO 0 (configurable via serial command)
* Pseudoclock 1 trigger input: GPIO 2 (configurable via serial command)
* Pseudoclock 2 trigger input: GPIO 4 (configurable via serial command)
* Pseudoclock 3 trigger input: GPIO 6 (configurable via serial command)
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
Communication during buffered execution is allowed (it is handled by a separate core and will not interfere with the DMA transfer of instruction data to the Pico's PIO cores).

## Supported serial commands.
Note: the commands are only read if terminated with `\r\n`.

* `version`: Responds with a string containing the firmware version.
* `board`: Responds with a string containing the board version (`pico1` or `pico2`).
* `status`: Responds with a string containing the PrawnBlaster status in the format `run-status:<int> clock-status:<int>` where the `run-status` integer is `0=manual-mode, 1=transitioning to buffered-execution, 2=buffered-execution, 3=abort requested, 4=currently aborting buffered execution, 5=last buffered-execution aborted, 6=transitioning to manual-mode`. `clock-status` is either 0 (for internal clock) or 1 (for external clock).
* `getfreqs`: Responds with a multi-line string containing the current operating frequencies of various clocks (you will be most interested in `pll_sys` and `clk_sys`). Multiline string ends with `ok\n`.
* `abort`: Prematurely ends buffered-execution.
* `setclock <mode:int> <freq:int>`: Reconfigures the clock source. See below for more details.
* `setnumpseudoclocks <number:int>`: Set the number of independent pseudoclocks. Must be between 1 and 4 (inclusive). Default at boot is 1. Configuring a number higher than one reduces the number of available instructions per pseudoclock by that factor. E.g. 2 pseudoclocks have 15,000 instructions each. 3 pseudoclocks have 10,000 instructions each. 4 pseudoclocks have 7,500 instructions each.
* `getwait <pseudoclock:int> <wait:int>`: Returns an integer related to the length of wait number `wait` for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). `wait` starts at `0`. The length of the wait (in seconds) can be calculated by subtracting the returned value from the relevant wait timeout and dividing the result by the clock frequency (by default 100 MHz). A returned value of `4294967295` (`2^32-1`) means the wait timed out. There may be more waits available than were in your latest program. If you had `N` waits, query the first `N` values (starting from 0). Note that wait lengths and only accurate to +/- 1 clock cycle as the detection loop length is 2 clock cycles. Indefinite waits should report as `4294967295` (assuming that the trigger pulse length is sufficient, see the FAQ below). Can be queried during buffered execution and will return `wait not yet available` if the wait has not yet completed.
* `start`: Immediately triggers the execution of the instruction set.
* `hwstart`: Triggers the execution of the instruction set(s), but only after first detecting logical high on the trigger input(s).
* `set <pseudoclock:int> <addr:int> <half-period:int> <reps:int>`: Sets the values of instruction number `addr` for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). `addr` starts at `0`. `half-period` is specified in clock cycles and must be at least `5` (and less than 2^32) for a normal instruction. `reps` should be `1` or more (and less than 2^32) for a normal instruction and indicates how many times the pulse should repeat. Special instructions can be specified with `reps=0`. A stop (end execution) instruction is specified by setting both `reps` and `half-period` to `0`. A wait instruction is specified by `reps=0` and `half-period=<wait timeout in clock cycles>` where the wait-timeout/half-period must be at least 6 clock cycles. Two waits in a row (sequential PrawnBlaster instructions) will trigger an indefinite wait should the first timeout expire (the second wait timeout is ignored and the length of this wait is not logged). See below (FAQ) for details on the requirements for trigger pulse lengths.
* `get <pseudoclock:int> <addr:int>`: Gets the half-period and reps of the instruction at `addr` for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). Return values are integers, separated by a space, in the same format as `set`.
* `setb <pseudoclock:int> <start addr:int> <instruction count:int>`: Sets the values of instructions number `start addr` through `start addr + instruction count` for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). `addr` starts at `0`. After this command is sent, PrawnBlaster reads `instruction count` 8 byte packets and decodes them into instruction values. The first 4 bytes of each packet are `reps` and the second 4 bytes are `half period`, each encoded as an unsigned little-Endian 32 bit integer. Instructions are then processed the same way as `set` (including stop instructions and wait instructions).
* `go high <pseudoclock:int>`: Forces the GPIO output high for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). This is useful for debugging.
* `go low <pseudoclock:int>`: Forces the GPIO output low for the pseudoclock `pseudoclock` (pseudoclock is zero indexed). This is useful for debugging.
* `setinpin <pseudoclock:int> <pin:int>`: Configures which GPIO to use for the pseudoclock `pseudoclock` trigger input (pseudoclock is zero indexed). Defaults to GPIO 0, 2, 4, and 6 for pseudoclocks 0, 1, 2 and 3 respectively. Should be between 0 and 19 inclusive. Trigger inputs can be shared between pseudoclocks (e.g. `setinpin 0 10` followed by `setinpin 1 10` is valid). Note that different defaults may be used if you explicitly assign the default for another use via `setinpin` or `setoutpin`. See FAQ below for more details.
* `setoutpin <pseudoclock:int> <pin:int>`: Configures which GPIO to use for the pseudoclock `pseudoclock` output (pseudoclock is zero indexed). Defaults to GPIO 9, 11, 13, and 15 for pseudoclocks 0, 1, 2, and 3 respectively. Should be between 0 and 19 inclusive or 25 (for the LED - useful for debugging without an oscilloscope). Must be unique for each output. Note that different defaults may be used if you explicitly assign the default for another use via `setinpin` or `setoutpin`. See FAQ below for more details.
* `getinpin <pseudoclock:int>`: Gets the currently set trigger input pin for pseudoclock `pseudoclock`. Returns either an integer corresponding to the set pin or `default` to indicate it will try and use the default pin as defined above for `setinpin`. See FAQ below for more details on what happens if it can't use the default.
* `getoutpin <pseudoclock:int>`: Gets the currently set trigger output pin for pseudoclock `pseudoclock`. Returns either an integer corresponding to the set pin or `default` to indicate it will use try and use the default pin as defined above for `setoutpin`. See FAQ below for more details on what happens if it can't use the default.
* `debug <state:str>`: Turns on extra debug messages printed over serial. `state` should be `on` or `off` (no string quotes required).
* `setpio <core:int>`: Sets whether the PrawnBlaster should use pio0, pio1, or pio2 in the RP2040/RP2350 chip (each have 4 state machines). Defaults to `0` (pio0) on powerup. May be useful if your particular board shows different timing behaviour (on the sub 10ns scale) between the PIO cores and you care about this level of precision. Otherwise you can leave this as the default. The RP2040 only supports pio0 and pio1.
* `program`: Equivalent to disconnecting the Pico, holding down the "bootsel" button, and reconnecting the Pico. Places the Pico into firmware flashing mode; the PrawnBlaster serial port should disappear and the Pico should mount as a mass storage device.

## Reconfiguring the internal clock.
The clock frequency (and even source) can be reconfigured at runtime (it is initially set to 100 MHz on every boot).
To do this, you send the command `setclock <mode:int> <freq:int>`, where the parameters are:

* mode: `0` means use the internal reference source and PLL. `1` means use an external source (bypassing the PLL) fed in on GPIO 20. `2` means use an external source (bypassing the PLL fed in on GPIO 22).
* freq: The clock frequency, in Hz (max 133000000 for RP2040 boards and 150000000 for RP2350 boards).

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

Note: Officially, the documentation for the Pico says external clock sources can only be up to 50MHz. We have successfully tested up to 133MHz (see [#6](https://github.com/labscript-suite/PrawnBlaster/issues/6)).
We recommend you personally verify the output of the PrawnBlaster is as expected if running from an external reference above 50MHz.

## Compiling the firmware

If you want to make changes to the firmware, or want to compile it yourself (because you don't trust binary blobs from the internet), we provide a docker configuration to help you do that.

1. Install docker desktop and make sure it is running (if you are on Windows, you may have to mess around a bit to get virtualisation working at an operating system level)
2. Clone this repository
3. Open a terminal with the current working directory set to the repository root (the `docker-compose.yaml`` file should be there)
4. Run `docker compose build --pull` to build the docker container
5. Run `docker compose up` to build the PrawnBlaster firmware.

Step 4 will take a while as it has to build the docker container.
If it is slow to download packages from the Ubuntu package repositories, consider providing an explicit apt mirror that is fast for you: `docker compose build --pull --build-arg APT_MIRROR="http://azure.archive.ubuntu.com/ubuntu/"`.

If you want to change which version of the pico SDK it builds against, this is set in the `build/docker/Dockerfile` file.
Just change the git tag of the pico SDK that gets cloned out by git, then rebuild the docker container (see step 4).

Note once the docker container is built, you can run step 5 as many times as you like.
You do not need to rebuild the container, even if you make changes to the PrawnBlaster source code.
You only need to rebuild the docker container if you modify the `build/docker/Dockerfile` file.

By default, running `docker compose up` builds the all variations of the firmware.
If you only want to build for a specific board, run either `docker compose up build_rp2040_firmware` or `docker compose up build_rp2350_firmware`.

The firmware will be located in `build_rp2xxx/prawnblaster/prawnblaster_rp2xxx[_overclock].uf2` where `rp2xxx` will be either `rp2040` or `rp2350`.
The firmware supporting overclocking will have the `_overclock` suffix.

## FAQ:

### Why is it called a "PrawnBlaster"?
The PrawnBlaster is named after the Australian $5 note, which is colloquially known as a "Prawn". 
This follows the tradition started by the PineBlaster which was named after the Australian $50 note (colloquially known as the "Pineapple").

### What are the trigger pulse requirements?
The initial start trigger (if using `hwstart`) and standard waits should only require a trigger pulse that is 4 clock cycles long (there is a 2 clock cycle buffer in the Pico GPIO design to reject spurious pulses).

Note that using indefinite waits requires that your trigger pulse is at least 12 clock cycles long and that it does not go high until 4 clock cycles after the previous instruction has completed.
If this requirement is not met, you may find that your first wait (in the indefinite wait) reports a wait length and/or the second (indefinite) wait is not immediately processed until a subsequent trigger pulse. This is due to the architecture of how indefinite waits are defined (as two sequential waits).

### What are the default pins?
While we have specified defaults for input/output pins (see serial commands above), there are circumstances where this will not happen.
For example, the default input pin for pseudoclock 0 is GPIO 0.
If this was explicitly assigned to pseudoclock 1 (as either an input or an output - e.g. `setoutpin 1 0`), then `getinpin 0` will still report `default` until you attempt to use any of the I/O of the PrawnBlaster.
At this point, defaults will be assigned to explicit pins.
Pin 0 would usually be assigned as the trigger input pin for pseudoclock 0, but its already in use because it was explicitly set for pseudoclock 1 output.
If a default pin is in use, we look for the lowest pin number that is not in use, and use that instead.
After using PrawnBlaster I/O, a call to `getinpin`/`getoutpin` will report the actual pin in use and not `default`.

What this ultimately means is that if you use a mixture of explicit pin sets and leave the remaining on default, you may not actually get the defaults we list (depending on what pins you have reallocated).
If you want to find out what pins, you should
1. Set the number of pseudoclocks in use
2. Set an explicit pins you care about with `setinpin`/`setoutpin`.
3. Send `go low 0` to force defaults to be assigned explicit pins (this is the simplest command that uses an output).
4. Send `getinpin 0`, `getoutpin 0`, (etc. if using more than 1 pseudoclock) in order to determine which GPIO pins have actually been assigned to each pseudoclock.

### Can I overclock the Pico board?
Yes, some people have reported being able to significantly overclock their Raspberry Pi Pico boards.
While we do not recommend you do so (nor will we be liable for any damage to your Pico or attached devices should you overclock it), we have provided a version of the PrawnBlaster firmware with no upper limit to the clock frequency here: [pico - RP2040](https://github.com/labscript-suite/PrawnBlaster/releases/latest/download/prawnblaster_rp2040_overclock.uf2), [pico 2 - RP2350](https://github.com/labscript-suite/PrawnBlaster/releases/latest/download/prawnblaster_rp2350_overclock.uf2)).
Use at your own risk!
We provide no support for the overclockable firmware.
If you need to tweak any other operating parameters of the Pico in order to achieve a stable overclock, you will need to manually modify the firmware source code and recompile it with those changes.

### Are all input pins equal?
It seems the answer to that is no - some input pins seem to have a different delay in processing triggers than others.
There are more details in [#5](https://github.com/labscript-suite/PrawnBlaster/issues/5), **but this will not affect users of the labscript suite** since all 4 pseudoclocks will be configured to trigger from the same input pin by the labscript suite device class.

If you are using the PrawnBlaster outside of the labscript suite, and want each pseudoclock to be triggered by a different GPIO pin, then you should be aware there seems to be a +/-10ns fluctuation in trigger response which may lead pseudoclocks to become out of sync with each other by 20ns per trigger.

### Can I use other RP2040/RP235x boards?
Maybe - it depends what pins are available.
You will also (most likely) need to recompile the firmware for these boards (and may need to adjust build options defined in various configuration files).
In general it should work as long as it has 9 GPIO pins available for use (and not hardwired to a peripheral) and one of them is pin 20/22.
Without pin 20 or 22, you can't externally reference the board.
And with less GPIO pins you can't have 4 independent pseudoclocks each with an independent trigger.
If you only need 1 trigger, you can get away with 1 pin for a trigger, 1 pin for each pseudoclock output you want (somewhere between 1 and 4) and 1 pin (either GPIO 20 or 22) for externally referencing if you need it.
But unless you have a strong reason to get another RP2040/RP235x based board, we suggest sticking with the standard Rapsberry Pi Pico or Pico 2 (Which is usually the cheaper option anyway).
As we are a volunteer project, we can only offer minimal support for other potentially compatible boards.
