//! Global allocator forwarding to ShirleyOS heap (kmalloc/kfree)

use core::alloc::{GlobalAlloc, Layout};
use crate::ffi::{shirley_heap_allocate, shirley_heap_deallocate};

pub struct ShirleyHeapAllocator;

unsafe impl GlobalAlloc for ShirleyHeapAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        shirley_heap_allocate(layout.size(), layout.align())
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        shirley_heap_deallocate(ptr);
    }
}

#[global_allocator]
pub static ALLOCATOR: ShirleyHeapAllocator = ShirleyHeapAllocator;
