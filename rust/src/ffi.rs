// rust/src/ffi.rs
use crate::allocator::ALLOCATOR;
use core::arch::asm;
use crate::fs::{VFS, seed as fs_seed};
use core::slice;

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

/// C entry point: build the initial ramdisk. Called from kernel_main
/// once, right after rust_heap_init().
#[unsafe(no_mangle)]
pub extern "C" fn fs_init() {
  let _g = IrqGuard::new();
  fs_seed();
}


/// C entry point: resolve `path` to a NodeId. Returns 0 for "not found",
/// anything > 0 is the NodeId. C callers will treat the NodeId as an
/// opaque u32. Only fs_read / fs_close do anything with it.
#[unsafe(no_mangle)]
pub extern "C" fn fs_open(path_ptr: *const u8, path_len: usize) -> u32 {
  if path_ptr.is_null() || path_len == 0 { return 0; }
  let _g = IrqGuard::new();
 
  // SAFETY: the caller promises (path_ptr, path_len) is a valid byte
  // range in the *kernel* address space. Post 12 copies user bytes
  // into a kernel buffer before calling this; that copy is in C.
  let bytes = unsafe { slice::from_raw_parts(path_ptr, path_len) };
  let path  = match core::str::from_utf8(bytes) {
      Ok(s)  => s,
      Err(_) => return 0,
  };
 
  VFS.lock().resolve(path).unwrap_or(0)
}

/// C entry point: copy bytes from `id` at `offset` into the kernel
/// buffer `dst_ptr`. Returns the number of bytes copied, or -1 on a
/// type error (e.g. reading a directory).
#[unsafe(no_mangle)]
pub extern "C" fn fs_read(
  id:       u32,
  offset:   usize,
  dst_ptr:  *mut u8,
  dst_len:  usize,
) -> i64 {
  if dst_ptr.is_null() || dst_len == 0 { return 0; }
  let _g = IrqGuard::new();
 
  // SAFETY: same story as fs_open. The caller promises `dst_ptr` is
  // a valid kernel-side buffer of length `dst_len`. The user's buffer
  // was already copied-out of by the C syscall handler.
  let dst = unsafe { slice::from_raw_parts_mut(dst_ptr, dst_len) };
  match VFS.lock().read(id, offset, dst) {
      Some(n) => n as i64,
      None    => -1,
  }
}

/// C entry point: close a NodeId. For now this is a no-op at the VFS
/// layer. The per-task fd table is what's actually "closed" in C.
/// Exported for symmetry and so Post 13's pipe support can hang off it.
#[unsafe(no_mangle)]
pub extern "C" fn fs_close(_id: u32) -> i64 {
  0
}

/// C entry point: for a directory NodeId, copy up to `cap` names into
/// `dst` as NUL-separated strings, and return how many names were
/// written. Returns -1 if `id` is not a directory.
#[unsafe(no_mangle)]
pub extern "C" fn fs_readdir(id: u32, dst_ptr: *mut u8, cap: usize) -> i64 {
    if dst_ptr.is_null() || cap == 0 { return 0; }
    let _g = IrqGuard::new();

    // SAFETY: caller promises (dst_ptr, cap) is a valid kernel-side
    // buffer, exactly as in fs_read.
    let dst = unsafe { slice::from_raw_parts_mut(dst_ptr, cap) };
    VFS.lock().readdir(id, dst)
}
