# PurgatoryOS

A bare-metal AArch64 kernel, written from scratch in C, assembly, and Rust — built chapter by chapter in the blog series [**From Silicon to Shell**](https://www.biosconfessions.com/posts/from-silicon-to-shell).

No libc. No host OS. Just your code and the silicon.

Starting from a cold CPU reset on QEMU's `virt` machine, the series walks through everything needed to reach a working interactive shell:

- **Boot** — an ARM64 assembly stub: EL2 → EL1, BSS, stack, jump to `kernel_main`
- **UART driver** — MMIO on the PL011, your first `kprint`
- **Virtual memory** — page tables, translation levels, and the MMU
- **Interrupts** — exception vectors, the GICv2, and the ARM Generic Timer
- **Heap allocator** — bump allocator, then a free-list allocator with coalescing
- **Processes** — a round-robin scheduler and hand-written context switching
- **System calls** — EL0 tasks trapping into the kernel via `svc`
- **Rust** — replacing the C allocator with a `no_std` Rust module over FFI
- **Filesystem** — an in-memory ramdisk with `open`/`read`/`close`
- **Shell** — a REPL over the UART with `ls`, `cat`, `echo`, and friends

## How this repo works

Each chapter of the series has its own branch containing the code **as it exists at the end of that chapter**. To follow along or check your work:

```sh
git clone https://github.com/nth4n-dev/PurgatoryOS.git
cd PurgatoryOS
git checkout post-4   # code as of Chapter 4
```

`main` holds this overview. New branches are pushed as chapters are released.

## Chapters

| # | Chapter | Branch |
|---|---------|--------|
| 1 | [Why Build an OS? The Case for Going All the Way Down](https://www.biosconfessions.com/posts/from-silicon-to-shell/1-why-build-an-os) | — |
| 2 | [Toolchain & Environment Setup: Your First ARM Binary](https://www.biosconfessions.com/posts/from-silicon-to-shell/2-toolchain-setup) | [`post-2`](https://github.com/nth4n-dev/PurgatoryOS/tree/post-2) |
| 3 | [The ARM Boot Process: From Reset Vector to kernel_main](https://www.biosconfessions.com/posts/from-silicon-to-shell/3-the-arm-boot-process) | [`post-3`](https://github.com/nth4n-dev/PurgatoryOS/tree/post-3) |
| 4 | [Hello, UART: Your First Kernel Output via the PL011](https://www.biosconfessions.com/posts/from-silicon-to-shell/4-hello-uart) | [`post-4`](https://github.com/nth4n-dev/PurgatoryOS/tree/post-4) |
| 5 | [Memory Layout & the Linker Script](https://www.biosconfessions.com/posts/from-silicon-to-shell/5-memory-layout-and-the-linker-script) | [`post-5`](https://github.com/nth4n-dev/PurgatoryOS/tree/post-5) |
| 6 | [Virtual Memory & the MMU: Teaching the CPU to Lie](https://www.biosconfessions.com/posts/from-silicon-to-shell/6-virtual-memory-and-the-mmu) | *coming soon* |
| 7 | [Exceptions & Interrupts: Teaching Your Kernel to Listen](https://www.biosconfessions.com/posts/from-silicon-to-shell/7-exceptions-and-interrupts) | *coming soon* |
| 8 | [A Heap Allocator: Teaching the Kernel to Manage Its Own Memory](https://www.biosconfessions.com/posts/from-silicon-to-shell/8-a-heap-allocator) | *coming soon* |
| 9 | Processes & Context Switching | *coming soon* |
| 10 | System Calls: Drawing the Line Between User and Kernel | *coming soon* |
| 11 | Bringing in Rust: A no_std Allocator Under the Kernel | *coming soon* |
| 12 | A Simple Filesystem: Turning the Heap Into a Ramdisk | *coming soon* |
| 13 | Building the Shell: A REPL Over the UART | *coming soon* |

## Prerequisites

You need an AArch64 cross-compiler and QEMU. Chapter 2 covers this in detail.

**macOS (Homebrew):**

```sh
brew install aarch64-elf-gcc qemu
```

**Ubuntu / Debian:**

```sh
sudo apt install gcc-aarch64-linux-gnu qemu-system-arm
```

## Build & run

```sh
make            # build kernel.elf
make run        # boot it in QEMU (-M virt -cpu cortex-a53)
make gdb        # boot halted, waiting for a GDB connection
```

The Makefile defaults to the `aarch64-elf-` toolchain prefix (Homebrew). On Linux, override it:

```sh
make CROSS=aarch64-linux-gnu- run
```

Exit QEMU with `Ctrl-A`, then `x`.

## Project layout

```
src/
├── arch/boot.S      # assembly entry point: EL2→EL1, BSS, stack
├── kernel.c         # kernel_main
├── drivers/uart.c   # PL011 UART driver (MMIO)
└── link.ld          # linker script: memory layout at 0x40000000
include/             # headers
Makefile             # build, run, gdb, dump targets
```

(Layout grows as the series progresses — later branches add `kernel/`, `mm/`, `rust/`, and more.)

## License

[MIT](LICENSE)
