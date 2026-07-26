// rust/src/ffi.rs
use crate::allocator::ALLOCATOR;
use core::arch::asm;
 
/// RAII: mask IRQs on construction, restore on Drop.
struct IrqGuard { prev: u64 }

impl IrqGuard {
  fn new() -> Self {
      let prev: u64;
      unsafe {
          asm!("mrs {0}, daif", out(reg) prev);
          asm!("msr daifset, #2");  // mask IRQ
      }
      Self { prev }
  }
}

impl Drop for IrqGuard {
  fn drop(&mut self) {
      unsafe {
          asm!("msr daif, {0}", in(reg) self.prev);
      }
  }
}

/// C entry point: initialise the heap from linker symbols.
#[unsafe(no_mangle)]
pub extern "C" fn rust_heap_init() {
  unsafe { ALLOCATOR.init_from_linker(); }
}

/// C entry point: allocate.  Takes over from kernel/heap.c's kmalloc.
#[unsafe(no_mangle)]
pub extern "C" fn kmalloc(size: usize) -> *mut u8 {
  let _g = IrqGuard::new();
  ALLOCATOR.alloc_raw(size)
}

/// C entry point: free.
#[unsafe(no_mangle)]
pub extern "C" fn kfree(ptr: *mut u8) {
  let _g = IrqGuard::new();
  unsafe { ALLOCATOR.free_raw(ptr); }
}
