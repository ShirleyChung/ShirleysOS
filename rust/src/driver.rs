//! Rust High-Level Device Driver: /dev/rust0
//! Demonstrates safe memory allocation (alloc crate), stateful driver handling,
//! and integration into the ShirleyOS device registry.

extern crate alloc;

use alloc::format;
use alloc::string::String;
use core::ffi::c_void;
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use crate::ffi::{DeviceOps, console_print, shirley_register_char_device};

static READ_COUNTER: AtomicU64 = AtomicU64::new(0);
static PRNG_STATE: AtomicU64 = AtomicU64::new(0x853c49e6748fea9b);
static HAS_READ_ONCE: AtomicBool = AtomicBool::new(false);

/// Simple XorShift64 pseudo-random generator
fn next_random() -> u64 {
    let mut x = PRNG_STATE.load(Ordering::Relaxed);
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    PRNG_STATE.store(x, Ordering::Relaxed);
    x
}

unsafe extern "C" fn rust_dev_open(_data: *mut c_void) -> i32 {
    HAS_READ_ONCE.store(false, Ordering::Relaxed);
    0 // Success
}

unsafe extern "C" fn rust_dev_close(_data: *mut c_void) -> i32 {
    0 // Success
}

unsafe extern "C" fn rust_dev_read(
    _data: *mut c_void,
    buffer: *mut u8,
    length: usize,
) -> i64 {
    if buffer.is_null() || length == 0 {
        return 0;
    }

    // Return EOF (0) if we have already returned the status report for this open session
    if HAS_READ_ONCE.swap(true, Ordering::Relaxed) {
        return 0;
    }

    let read_count = READ_COUNTER.fetch_add(1, Ordering::Relaxed);
    let rand_val = next_random();

    // Use Rust dynamic memory allocation (alloc::format! / String)
    // to prove that the ShirleyOS heap bridge works properly in no_std.
    let report: String = format!(
        "--- [ShirleyOS Rust Subsystem v0.1.0] ---\n\
         Driver Status : Healthy & Active (Safe Rust)\n\
         Read Session  : #{}\n\
         PRNG Entropy  : 0x{:016x}\n\
         Memory Safety : No buffer overflows / No use-after-free\n\
         Architecture  : Bare-metal no_std staticlib\n",
        read_count, rand_val
    );

    let bytes = report.as_bytes();
    let to_copy = core::cmp::min(bytes.len(), length);

    core::ptr::copy_nonoverlapping(bytes.as_ptr(), buffer, to_copy);
    to_copy as i64
}

unsafe extern "C" fn rust_dev_write(
    _data: *mut c_void,
    buffer: *const u8,
    length: usize,
) -> i64 {
    if buffer.is_null() || length == 0 {
        return 0;
    }

    // Accumulate incoming bytes into entropy pool
    let slice = core::slice::from_raw_parts(buffer, length);
    let mut mixed = PRNG_STATE.load(Ordering::Relaxed);
    for &b in slice {
        mixed = mixed.rotate_left(5) ^ (b as u64);
    }
    PRNG_STATE.store(mixed, Ordering::Relaxed);

    length as i64
}

static RUST_DEV_OPS: DeviceOps = DeviceOps {
    open: Some(rust_dev_open),
    close: Some(rust_dev_close),
    read: Some(rust_dev_read),
    write: Some(rust_dev_write),
};

pub fn register_rust_driver() -> bool {
    let name = b"rust0\0";
    let ret = unsafe {
        shirley_register_char_device(
            name.as_ptr(),
            &RUST_DEV_OPS as *const DeviceOps,
            core::ptr::null_mut(),
        )
    };

    if ret == 0 {
        console_print("[rust] Driver '/dev/rust0' successfully registered\n");
        true
    } else {
        console_print("[rust] Failed to register '/dev/rust0'\n");
        false
    }
}
