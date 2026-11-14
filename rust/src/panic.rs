//! Panic handler for ShirleyOS Rust subsystem

use core::panic::PanicInfo;
use crate::ffi::console_print;

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    console_print("\n[RUST PANIC] A panic occurred in ShirleyOS Rust subsystem!\n");
    if let Some(location) = info.location() {
        console_print("  File: ");
        console_print(location.file());
        console_print("\n");
    }
    loop {
        // Hang indefinitely in panic state
        core::hint::spin_loop();
    }
}
