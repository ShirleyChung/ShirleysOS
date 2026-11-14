//! ShirleyOS Rust Subsystem & Driver Framework
//! Provides high-level driver and subsystem abstractions on top of the
//! architecture-neutral ShirleyOS kernel.

#![no_std]

extern crate alloc;

pub mod allocator;
pub mod ffi;
pub mod panic;
pub mod driver;

use ffi::console_print;

/// ShirleyOS Rust subsystem initialization entry point.
/// Invoked by ShirleyOS kernel_main() after heap is up.
#[no_mangle]
pub extern "C" fn shirley_rust_init() -> i32 {
    console_print("[rust] ShirleyOS Rust Runtime initializing...\n");
    
    // Register the first safe Rust device driver
    if driver::register_rust_driver() {
        console_print("[rust] High-level Rust drivers ready.\n");
        0
    } else {
        -1
    }
}

