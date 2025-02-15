# RP2350 Powman States

## Description

An example of using the new powman states available in the RP2350 to go to a
lower power sleep (<500 uA) compared to the SLEEP and DORMANT states of the
RP2040.

This uses the powman BOOT registers to make the bootloader reboot to some other
address and uses the stack to jump back to the callee-address to resume work
like normal.

Without using the powman BOOT registers, changing to a lower power state will
reboot the core and run from the start of `main()` again, which isn't useful
for most applications that want sleep.

It leaves the SRAM domains on to retain any global state and stack contents.

Example is built for Arm but can be adapted to run on RISC-V cores.

## Explanation

I'd like to preface this information with the fact that I'm not a microcontroller
expert, nor have I used the RP2350 an awful lot (I mean, you can't even buy the
chip by itself at the time of writing) but I have dug into a reset sequence or
two in my time with computers.

To understand why powman sleep is done the way it is, we need to first look at
the power states provided by the RP2350's predecessor, the RP2040.
These states were called `SLEEP` and `DORMANT` and worked by stopping the clocks
that ran the cores while retaining the processor state like registers etc.
These states are still present on the RP2350 as well.
They reduced power by a lot, but there were still areas of the processor 
which were powered on that didn't need to be and resulted in a rather high sleep
current of 1-2 mA.
Not so great when other microcontrollers (like some STM32 series) can go to
sub-100 uA range in sleep modes.

So how does Raspberry Pi fix this problem? Well its simple: If you aren't using
the processor, then lets just turn it all off!
Well, technically it turns off the switched core and peripherals and retains a
small section of always-on (AON) peripherals, which contains the powman peripheral.
You can optionally power-down the RAM but if you want to retain information between
reboots, you probably won't want to do that...

Turning off the core results in a drastic power drop of around 10x, down as low
as 150 uA!
This is excellent! There's only one problem: We've just turned the processor off,
so when it powers on again, it won't have any "memory" of what it was doing.
Not knowing any different, it goes through the whole reset sequence and starts
from the beginning (bootloader and into `main()`) again.
To make things worse, the bootloader zeroes all of memory as part of its
power-on sequence and resets all global variables to their initial values.
So even though we kept the RAM on during sleep, using up all that power, we don't
get to reap the benefits of keeping it around!
This isn't exactly ideal if you wanted to sleep for 30 seconds before checking
a sensor again and comparing it to its last reading which you carefully tucked
away in memory.

This means you need to have some way of differentiating between power-on resets
(first powering the pico on) and wakeups from the power manager.
If not, you'll end up going through your sensor initialisation sequence and
reset whatever it was doing in the process.

Fortunately, Raspberry Pi foresaw this problem and gave us a few special registers
which are retained when we go to sleep.
These are the `BOOT[0..3]` registers in the powman peripheral, which is always
kept on, even in the lowest sleep states.

These `BOOT` registers are special in that you can store a return address and
stack pointer in them so that when the processor turns back on, the bootloader 
can check them and, if present and correct, jump to the code which it points to.
This re-entry code can then re-initialise the peripherals *without* going through
your sensor reset sequence, because we know that if it runs, its because
we woke up from sleep so can assume that the sensor is already up and running.
This skips the whole "RAM re-initialise" bit and retains your global variables
as well!

The catch is that the code to jump to needs to be loaded into RAM before
the processor sleeps.
Typically, pico code is run directly off of flash memory, so we need to do some
work and load it into RAM first.
The effect is that (at least some of) the RAM needs to be kept powered on during
sleep to keep the re-entry code and corresponding stack around for wakeup.

This requires some effort, and while it isn't a lot of work, you have to be
careful about it.
Unfortunately, I haven't found any documentation online that says you need to
go through all of this to use the sparkly new power-saver features of the RP2350
in a way that's useful for at least a large number of applications.

Hopefully this explanation can help people understand *why* all of this work is
needed and how to get it set up.

But why is it so complicated for th RP2350? I can sleep my SAMD microcontroller
with a single `wfi` instruction and wake up exactly where I left off, all while
drawing the same amount of power.
While I'm not a microcontroller expert, my best guess is that silicon design is
hard and expensive.

The RP2350 is designed to be a low-cost microcontroller, only at around $1 per
chip.
Compare this to other microcontrollers like the STM32 or SAMD which, for roughly
the same set of features, typically cost more money.

In order to do all of this wakeup reset stuff opaquely (without the programmer
seeing it), there needs to be silicon to do it instead.
I mean, the work still needs to get done, right? And if we aren't doing it,
the hardware has to instead.

The problem is that more hardware features means more silicon, which means more
design time, validation, testing, chances for errors, and simply takes up more
silicon space, all of which increase cost.
To keep the prices as low as they are, Raspberry Pi has opted to not do this in
hardware and make the programmer do it instead.
I feel like that's a fair trade-off really.
A couple hundred bytes of code in exchange for a cheaper microcontroller.

Will this change in the future? Who knows, maybe the third series of RP
microcontrollers will have automatic sleep so we don't need to do all of this,
but that's a long way away, so I won't hold by breath.

## With more Technical Details

Now, this is the part where I admit that I don't know *exactly for sure* what
is happening under the hood, but I think that my guesses are sensible.
If Raspberry Pi wants to confirm this or update their documentation with more
details then I'd love to see it.

So, according to the RP2350 datasheet[^2] the new powman peripheral does, indeed,
power off the switched core inside the RP2350, including peripherals, bus fabric
and more.
This effectively powers off the whole microcontroller, sans the RAM, XIP cache
and always-on (AON) domain.
Inside this AON domain is the powman peripheral, containing the
low-power oscillator (LPOSC), RTC timer and power control.
Its this always-on logic that allows the microcontroller to wake itself up.
You can configure up to four wake-up sources from either GPIO inputs or the AON
timer.

When the powman receives a wake up event, it powers things up again.
The core, having just been powered on and retaining no information, starts
execution from the bootloader, and passes control to `boot_stage2`.

`boot_stage2` does lots of things[^4], too many to discuss here, but
one important thing is initialising the C runtime, called `crt0`.
This is common on even desktop PCs and it sets up the processor and memory to
be in a state that can run C code.
It involves running constructors, zeroing the BSS section of RAM
and copying initial global variable values to this area, among other things.

This is really helpful and all, but the problem comes when we already have RAM
set up in a way we want, with nice data that we have carefully arranged and kept
alive while we powered down the core.
When `boot_stage2` is run on a reset, and is not instructed to do otherwise, it
re-initialises the C runtime and overwrites all the RAM we care about.

To avoid this during powman resets, `boot_stage2` checks the four `BOOT` registers
in the powman peripheral[^5] and jumps to that code instead of carrying on with
the typical C runtime initialisation and jump to `main()`[^1].

The datasheet states that this code needs to be stored in RAM as opposed to
typical flash.
I don't particularly know why this limitation exists, but I don't imagine it's
there for no reason.

The powman `BOOT` registers store both a reset vector and a stack pointer, so
you can run the code with the same stack frame as when going to sleep.
This means that, if you're careful with the stack, the reset vector function can
return normally and pop off the return address which the last function put there
and move back to where the program was before it went to sleep.
It also retains the invariants of the ARM calling convention as a handy
side-effect.
It's a clever way of retaining program state while fully powering down the core.

This, of course, means that if you want to retain your location in code, you
also need to keep the RAM powered up, reducing possible power savings.
But if you were worried about returning to where you left off, you probably have
some sort of program state that you wanted to retain anyway and were unlikely
to want to turn off the RAM in the first place, so it's not really an issue in
reality.

While we want to avoid the standard initialisation sequence to keep our RAM
intact, we still need to run *some* initialisation functions like properly
resetting the peripherals so we can set them up again properly.
fortunately, in the pico SDK crt0.S[^6] it writes RAM and then calls a function
`runtime_init()` provided by the pico SDK.
Once that's one, it calls `main()` and `exit()` like a typical desktop program
(except `exit()` shouldn't return on an embedded system).


## Credits and Resources

Credits to peterharperuk for publishing a nice [powman example](https://github.com/peterharperuk/pico-examples/commit/7dccd00d15ded4ddf961f44fdcd1f11a9d8c8be1)
which helped me start investigating "properly" the powman.

[^1]: RP2350 datasheet, section 5.2:
[^2]: RP2350 datasheet, section 6.3:
[^5]: RP2350 datasheet, section 6.4:
[^3]: Kevin Boone's explanation of running code in RAM: <https://kevinboone.me/pico_run_ram.html>
[^4]: `boot_stage2` in Pico-SDK GitHub: <>

## License

Do whatever you want with this code and information.

