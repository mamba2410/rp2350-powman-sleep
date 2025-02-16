#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "hardware/flash.h"
#include "hardware/powman.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/platform/sections.h" // for `__not_in_flash_func()`
#include "pico/runtime_init.h"
#include "pico/stdlib.h"
#include "pico/sync.h" // for `__wfi()`
                       
#define POWMAN_BOOT_MAGIC_NUM 0xb007c0d3
#define POWMAN_BOOT_MAGIC_NEG 0x4ff83f2d

// Global state to make sure 
static uint64_t next_wakeup = 0;
static uint64_t const WAKEUP_INTERVAL = 5000;

// Example global state which needs to be preserved on reboot 
static uint32_t led_off_time = 250;
static uint32_t led_on_time = 500;

volatile bool debug_catch = true;

/**
 *  If something goes wrong, blink rapidly with this pattern.
 */
void error_blink() {
    while(1) {
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(150);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(150);
    }
}


/**
 *  Initialise the peripherals in a separate function since they need to be 
 *  initialised after every sleep reboot.
 */
void init_peripherals() {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}


/**
 *  This code should be run from RAM with a stack pointer set by `do_sleep()`
 *  When this function returns, it goes back to `main()`
 */
void __not_in_flash_func(exit_sleep)() {

    // Check what core we are. We only want core0 running.
    //volatile int *proc_num = (volatile int*)(SIO_BASE + SIO_CPUID_OFFSET);
    //while(*proc_num) {}


    // Now that we know we're core0, we stop core1
    //multicore_reset_core1();
    
    //while(debug_catch);
    //debug_catch = 1;

    //__breakpoint();

    // Set flash to run in XIP mode because we skipped the booloader
    //rom_flash_exit_xip();
    //rom_bootrom_state_reset(BOOTROM_STATE_RESET_CURRENT_CORE);
    rom_flash_flush_cache();
    rom_flash_enter_cmd_xip();
    //flash_enable_xip_via_boot2();
    //_stage2_boot();

    __breakpoint();

    //while(debug_catch);
    //debug_catch = 1;

    // Initialise the C library for core0 without destroying our RAM
    runtime_init();
    //runtime_run_per_core_initializers();

    //while(debug_catch);
    //debug_catch = 1;
    __breakpoint();

    //stdio_init_all();
    //printf("awake");
    //stdio_flush();

    init_peripherals();

    // Test to see if re-initialise was successful
    while(1) {
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(150);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(150);
    }

}


/**
 *  We need a separate function to do this to not mess up the stack frame
 */
void set_next_timer() {
    // Set powman timer alarm
    next_wakeup += WAKEUP_INTERVAL;
    powman_enable_alarm_wakeup_at_ms(next_wakeup);

}


/**
 *  Prepares the system for sleep by setting the powman alarm to self-wakeup.
 *  Also loads the `exit_sleep()` function into RAM to enable rebooting into
 *  it after wakeup.
 */
void do_sleep() {

    set_next_timer();

    // Set the new `exit_sleep()` RAM function as reboot vector
    uintptr_t boot_addr = (uintptr_t)exit_sleep | 1; // OR with 1 ensures we are running ARM thumb instructions rather than RISC-V
    uintptr_t stack_pointer;

    // We need assembly to get the stack pointer
    asm (
        "mov %0, sp"
        : "=r" (stack_pointer)
    );

    extern char __StackTop;
    stack_pointer = (uintptr_t)&__StackTop;

    printf("boot_addr: %p\n", boot_addr);
    printf("stack_pointer: %p\n", stack_pointer);
    extern uintptr_t __preinit_array_start; 
    printf("__preinit_array_start: 0x%p\n", &__preinit_array_start);
    stdio_flush();

    powman_hw->boot[0] = POWMAN_BOOT_MAGIC_NUM; // magic_number
    //powman_hw->boot[0] = 0;
    powman_hw->boot[1] = POWMAN_BOOT_MAGIC_NEG ^ boot_addr; // (-magic_number) ^ boot_addr
    powman_hw->boot[2] = stack_pointer; // stack pointer
    //powman_hw->boot[2] = 0x20081000; // Top of ram
    powman_hw->boot[3] = boot_addr; // boot_addr

    powman_power_state p0_0 = 0x0f; // SW core, XIP cache, SRAM 0, SRAM 1
    powman_power_state p1_0 = 0x07; // XIP cache, SRAM 0, SRAM 1
    //powman_power_state p0_0 = 0x0c; // SW core, XIP cache, SRAM 0, SRAM 1
    //powman_power_state p1_0 = 0x00; // nothing

    bool valid = powman_configure_wakeup_state(p1_0, p0_0);
    if(!valid) error_blink(); // 

    gpio_deinit(PICO_DEFAULT_LED_PIN);

    powman_set_power_state(p1_0);
    __wfi();
    // Goes to sleep here and returns to original caller

    // Reboots into bootloader which runs the code from the boot vector above.
    // When that function returns, it goes back to the place this function was called.

    while(1) {
        error_blink();
    }
}


/**
 * Pretend like this is useful work which depends on some global state.
 * Blink the LED in different patterns to show that it works.
 */
void do_work() {
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(led_on_time);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(led_off_time);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(led_on_time);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}


int main() {
    // Enter here from normal reboot when powman reset vector is empty.

    stdio_init_all();

    init_peripherals();

    // Set and start the powman timer once
    powman_timer_set_1khz_tick_source_lposc();
    powman_timer_set_ms(0x123456);
    next_wakeup = 0x123456;
    powman_timer_start();

    // Allow power down when debugger connected
    powman_set_debug_power_request_ignored(true);


    /*
     * Infinite loop to do work and sleep
     */
    while(1) {

        // useful work
        do_work();

        // Go to sleep for some time
        do_sleep();

    }

}

