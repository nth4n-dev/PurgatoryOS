/* src/arch/boot.S */
.section .text.boot
.global _start

_start:
  /* Check current Exception Level */
  mrs  x0, CurrentEL
  lsr  x0, x0, #2
  cmp  x0, #1
  beq  .el1_ready

  /* If in EL2 (QEMU default), drop to EL1 */
  mov  x0, #(1 << 31)   /* RW=1 (EL1 is AArch64) */
  msr  hcr_el2, x0

  mov  x0, #0x3c5       /* EL1h with DAIF masked */
  msr  spsr_el2, x0

  adr  x0, .el1_ready
  msr  elr_el2, x0

  eret

.el1_ready:
  /* --- FIX 1: Set Exception Vector Base Address --- */
  /* This ensures that if a fault occurs, the CPU jumps to your 
     actual vector code instead of address 0x0/0x200. */
  ldr  x0, =vectors
  msr  vbar_el1, x0
  isb

  /* --- FIX 2: Enable FP/SIMD at EL1 --- */
  /* Prevents Undefined Instruction exception when GCC uses
     NEON/SIMD registers (Q0-Q31) for zeroing BSS or copying memory. */
  mov  x0, #(3 << 20)   /* CPTR_EL1.FPEN = 0b11 (Enable access) */
  msr  cpacr_el1, x0
  isb

  /* Set up stack pointer */
  ldr  x0, =_stack_top
  mov  sp, x0

  /* Zero out BSS */
  ldr  x1, =__bss_start
  ldr  x2, =__bss_end
.zero_bss:
  cmp  x1, x2
  bge  .call_kernel
  str  xzr, [x1], #8
  b    .zero_bss

.call_kernel:
  /* Ensure memory is synced before jumping to C code */
  dsb  sy
  isb
  bl   kernel_main
  b    .                /* Halt if kernel_main returns */
