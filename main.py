import array, time
from machine import Pin
import rp2


@asm_pio(sideset_init=PIO.OUT_LOW)
def asm_prog():
    wrap_target()
    # Wait for main process to signal that the FIFO has had instructions placed
    # in it. In theory pull will block, but we pull out two instructions quickly
    # and I don't want to accidentally have the first pulse be too long if the
    # FIFO is not full enough!
    #
    # TODO: This needs to also include the statemachine ID, but I don't know how
    # to get it dynamically inside this function :/
    wait(1,irq,0x10|1)
    
    label("start")
    # Pull reps into OSR (blocking)
    pull()
    #
    # Start program and go high
    #
    # Move reps into Y and go high
    mov(y, osr).side(1)
    # This allows us to skip the above mov if we already did it as part of "newinst" below
    label("shortstart")
    # Some nops to balance out the paths
    nop()
    nop()
    # pull half period into OSR
    pull()
    label("mainloop")
    # (Re)load half period into X 
    mov(x, osr)
    label("highloop")
    # Decrement half period and jump if not 0. This loops for X clock cycles
    jmp(x_dec, "highloop")

    #
    # Reload half period into X and drop to low
    #
    mov(x, osr).side(0)
    label("lowloop")
    # Decrement half period and jump if not 0. This loops for X clock cycles
    jmp(x_dec, "lowloop")
    # Decrement reps now since we have the space, and jump to normal path if 
    # there are still more reps to do
    jmp(y_dec, "continuereps")
    # If we are out of reps, do the unusual path for a new instruction
    label("newinst")
    # Pull next reps into OSR
    pull()
    mov(y, osr)
    jmp(not_y, "wait")
    jmp("shortstart").side(1)
    
    #
    # Normal path when we still have more reps to do
    #
    label("continuereps")
    nop()
    nop()
    nop()
    # Go high and jump to point where we reload the half period
    nop().side(1)
    nop()
    nop()
    jmp("mainloop")

    #
    # Wait code
    #
    label("wait")
    # Load in half period
    pull()
    mov(x, osr)
    # if half period is also 0, then stop
    jmp(not_x, "stop")
    # otherwise to the wait loop
    label("waitloop")
    # jump if pin high
    jmp(pin, "waitdone")
    # decrement x
    jmp(x_dec, "waitloop")

    label("waitdone")
    # put X in ISR
    mov(isr, x)
    # send count to main program as length of wait (0 implies timeout)
    # The length of the wait is determined by the original timeout sent, minus
    # the value returned here, all times 2 times the clock cycle length
    push()
    # jump to start to resume
    jmp("start")


    # Stop code
    label("stop")
    # wrap to beginning and block on IRQ
    wrap()

