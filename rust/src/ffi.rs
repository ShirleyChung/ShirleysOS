//! C ABI bindings for ShirleyOS kernel bridge

use core::ffi::c_void;

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum DeviceType {
    Character = 0,
    Block = 1,
    Input = 2,
    Network = 3,
}

#[repr(C)]
pub struct DeviceOps {
    pub open: Option<unsafe extern "C" fn(driver_data: *mut c_void) -> i32>,
    pub close: Option<unsafe extern "C" fn(driver_data: *mut c_void) -> i32>,
    pub read: Option<unsafe extern "C" fn(driver_data: *mut c_void, buffer: *mut u8, length: usize) -> i64>,
    pub write: Option<unsafe extern "C" fn(driver_data: *mut c_void, buffer: *const u8, length: usize) -> i64>,
}

extern "C" {
    pub fn shirley_heap_allocate(bytes: usize, alignment: usize) -> *mut u8;
    pub fn shirley_heap_deallocate(ptr: *mut u8);
    pub fn shirley_console_write(ptr: *const u8, len: usize);
    pub fn shirley_register_char_device(
        name: *const u8,
        ops: *const DeviceOps,
        driver_data: *mut c_void,
    ) -> i32;
}

/// Print a string slice to the ShirleyOS kernel console
pub fn console_print(s: &str) {
    unsafe {
        shirley_console_write(s.as_ptr(), s.len());
    }
}
