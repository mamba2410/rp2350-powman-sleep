
// Put this in the .time_critical section to make sure it is in RAM, not flash
.section .time_critical.really_do_sleep
.global really_do_sleep
.type really_do_sleep, %function
really_do_sleep:
    // The reason this is in assembly and not C is because we need full control
    // over the stack and to get an address to the middle of the function.

    // Registers get wiped on reset, so we need to store them to RAM
    // so that when we return to the caller, they're still intact.
    // pc, sp get restored by bootloader, r0-2 are caller-saved
    // NOTE: This doesn't save the 32 FPU registers, s0-s31, but they should
    // also be pushed and popped
    // Pushing r8-r12 gives "invalid register list" errors, but they should also
    // be saved and restored
    push {r3, r4, r5, r6, r7, lr}

    // Get pointer to POWMAN BOOT registers
    ldr r0, powman_boot_base

    // Save stack pointer to BOOT2
    mov r1, sp
    str r1, [r0, #0x08]

    // Put magic value in BOOT0
    ldr r1, powman_magic_value
    str r1, [r0, #0x00]

    // Get address to resume execution from
    // Resume directly after the wfi instruction
    ldr r1, =really_do_sleep_continue // "=" means absolute address
    mov r2, #1
    orr r1, r2 // Set bottom bit to signal we're in ARM mode

    // Put reboot address in BOOT3
    str r1, [r0, #0x0c]

    // Calculate "checksum"
    // "checksum" = (-magic) ^ reboot_address
    ldr r2, powman_magic_value_negative
    eor r1, r2
    str r1, [r0, #0x04]

    // Power down
    wfi


    // Redirected here from bootloader
really_do_sleep_continue:

    // Clear BOOT register magic number, to ensure we won't accidentally
    // jump back here after pressing the RUN button
    ldr r0, powman_boot_base
    mov r1, #0
    str r1, [r0, #0x0]

    // Pop the registers from RAM, since they got wiped from reset
    // Pop lr straight into pc to return from this function like nothing happened
    pop {r3, r4, r5, r6, r7, pc}

//.short 0x0000 // Padding to align the below words to 4 bytes
powman_boot_base: .word 0x401000d0
powman_magic_value: .word 0xb007c0d3
powman_magic_value_negative: .word 0x4ff83f2d
