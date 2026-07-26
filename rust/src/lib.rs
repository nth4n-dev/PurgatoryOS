// rust/src/lib.rs
#![no_std]
#![feature(alloc_error_handler)]

extern crate alloc;   // needed for Box/Vec/etc. inside Rust

mod allocator;
mod fs;
mod ffi;

use core::panic::PanicInfo;

/// # Safety
/// Called by rustc when anything in this crate panics. A kernel has nowhere
/// to unwind to, so we halt the CPU. The `wfi` keeps the core idle rather
/// than busy-looping. Easier on power, friendlier on emulators.
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    // UART access through a raw volatile write. We deliberately do NOT call
    // back into C here. If kprint panicked inside us, recursive panics would
    // double-fault. Writing straight to the PL011 data register is the only
    // thing we trust from inside a panic path.
    let uart = 0x0900_0000 as *mut u32;
    for b in b"\n[rust panic] ".iter() {
        unsafe { core::ptr::write_volatile(uart, *b as u32); }
    }
    if let Some(loc) = info.location() {
        // File name and line number only. Formatting a PanicInfo allocates,
        // which we obviously can't do while holding the allocator's lock.
        for b in loc.file().as_bytes().iter().take(64) {
            unsafe { core::ptr::write_volatile(uart, *b as u32); }
        }
    }
    loop {
        unsafe { core::arch::asm!("wfi"); }
    }
}

/// Called if the global allocator returns null.  Our `kmalloc` never
/// returns null from inside Rust (it returns to C, which checks), but the
/// `alloc` crate's Box/Vec code paths need this symbol to exist.
#[alloc_error_handler]
fn oom(_: core::alloc::Layout) -> ! {
    panic!("kernel heap exhausted");
}
