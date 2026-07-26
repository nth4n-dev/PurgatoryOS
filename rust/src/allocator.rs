// rust/src/allocator.rs
use core::alloc::{GlobalAlloc, Layout};
use core::ptr::{self, NonNull};
use spin::Mutex;

/// 8-byte alignment. AArch64's minimum for 64-bit loads/stores.
/// Identical to Post 8's HEAP_ALIGN.
const HEAP_ALIGN: usize = 8;

/// The on-heap block header.  Layout matches kernel/heap.c's
/// block_header_t byte-for-byte so a pointer we hand to C and later
/// receive back from C lands on the right type.
///
/// Rust field ordering is deterministic with `#[repr(C)]`, so this is
/// a safe one-for-one mapping.
#[repr(C)]
struct Block {
    size:    usize,             // payload bytes, not counting this header
    is_free: u32,               // 1 = free, 0 = allocated (u32 for C compat)
    _pad:    u32,               // keep AArch64 alignment
    next:    Option<NonNull<Block>>,
}

impl Block {
    /// The 24-byte rounded header size. Same as HEADER_SIZE in heap.h.
    const HDR: usize = {
        let raw = core::mem::size_of::<Block>();
        (raw + HEAP_ALIGN - 1) & !(HEAP_ALIGN - 1)
    };

    /// Address of the payload the caller will see.
    fn payload_ptr(&mut self) -> *mut u8 {
        unsafe {
            (self as *mut Self as *mut u8).add(Self::HDR)
        }
    }
}

/// The allocator's internal state. A singly-linked list of blocks.
/// The entire chain lives *inside* the heap itself; we only keep one
/// pointer in .bss.
struct FreeList {
    head: Option<NonNull<Block>>,
}

// SAFETY: FreeList is only ever accessed inside a spin::Mutex, which
// serialises access. The raw pointers it holds never escape a
// MutexGuard's scope to another thread.
unsafe impl Send for FreeList {}

impl FreeList {
    const fn empty() -> Self {
        FreeList { head: None }
    }

    /// Called once at boot to lay the initial "one giant free block"
    /// across [start, end).  Unsafe because we're promising the range
    /// is actually usable memory.
    unsafe fn init(&mut self, start: *mut u8, end: *mut u8) {
        let total = end as usize - start as usize;
        assert!(total > Block::HDR + HEAP_ALIGN, "heap region too small");

        let block = start as *mut Block;
        (*block).size    = total - Block::HDR;
        (*block).is_free = 1;
        (*block)._pad    = 0;
        (*block).next    = None;

        self.head = NonNull::new(block);
    }


    /// First-fit allocation.  Returns a payload pointer on success,
    /// null if no suitable block is found.
    fn alloc_raw(&mut self, size: usize) -> *mut u8 {
        if size == 0 { return ptr::null_mut(); }
        let size = (size + HEAP_ALIGN - 1) & !(HEAP_ALIGN - 1);

        let mut cur = self.head;
        while let Some(mut node) = cur {
            // SAFETY: nodes are constructed at init() and split() and
            // never dangle while held in this list.  We're the sole
            // writer because of the enclosing Mutex.
            let block = unsafe { node.as_mut() };

            if block.is_free == 1 && block.size >= size {
                let remainder = block.size - size;

                // Split only if the remainder can hold a full header
                // plus HEAP_ALIGN bytes of payload.  Identical to the
                // `>` guard from Post 8's C version.
                if remainder > Block::HDR + HEAP_ALIGN {
                    // SAFETY: split_addr is inside the same 1 MB heap
                    // region we were handed at init().
                    let split_addr = unsafe {
                        block.payload_ptr().add(size)
                    } as *mut Block;

                    unsafe {
                        (*split_addr).size    = remainder - Block::HDR;
                        (*split_addr).is_free = 1;
                        (*split_addr)._pad    = 0;
                        (*split_addr).next    = block.next;
                    }

                    block.size = size;
                    block.next = NonNull::new(split_addr);
                }

                block.is_free = 0;
                return block.payload_ptr();
            }

            cur = block.next;
        }

        ptr::null_mut()
    }

    /// Mark a previously-allocated pointer as free, and coalesce
    /// forward if the next block is also free.  UB if ptr was not
    /// previously returned by alloc_raw.
    unsafe fn free_raw(&mut self, ptr: *mut u8) {
        if ptr.is_null() { return; }

        let block = (ptr as *mut u8).sub(Block::HDR) as *mut Block;
        (*block).is_free = 1;

        if let Some(mut next) = (*block).next {
            let nref = next.as_mut();
            if nref.is_free == 1 {
                (*block).size += Block::HDR + nref.size;
                (*block).next  = nref.next;
            }
        }
    }
}

pub struct Heap {
    inner: Mutex<FreeList>,
}

impl Heap {
    pub const fn new() -> Self {
        Heap { inner: Mutex::new(FreeList::empty()) }
    }

    /// Initialise from the linker-exported heap range.  Called once
    /// from the kernel, on the boot path, before any allocation.
    pub unsafe fn init_from_linker(&self) {
        extern "C" {
            static __heap_start: u8;
            static __heap_end:   u8;
        }
        let start = &__heap_start as *const u8 as *mut u8;
        let end   = &__heap_end   as *const u8 as *mut u8;
        self.inner.lock().init(start, end);
    }

    pub fn alloc_raw(&self, size: usize) -> *mut u8 {
        self.inner.lock().alloc_raw(size)
    }

    pub unsafe fn free_raw(&self, ptr: *mut u8) {
        self.inner.lock().free_raw(ptr)
    }
}

// GlobalAlloc impl (so Box/Vec work inside Rust)
unsafe impl GlobalAlloc for Heap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // Our free-list only handles HEAP_ALIGN alignment; anything
        // stricter is a caller error inside Rust.  Callers from C go
        // through kmalloc, which never asks for more than 8-byte.
        debug_assert!(layout.align() <= HEAP_ALIGN);
        self.alloc_raw(layout.size())
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        self.free_raw(ptr);
    }
}

#[global_allocator]
pub static ALLOCATOR: Heap = Heap::new();
