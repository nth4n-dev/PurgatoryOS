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
  /* Enable FP and SIMD at EL1. */
  /* Without this you get an Undefined Instruction exception when GCC optimises
     zeroing loops or memory copies using Q/V registers. */
  mov  x0, #(3 << 20)   /* CPTR_EL1.FPEN = 0b11 (No trap) */
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
  bl   kernel_main
  b    .                /* Halt if kernel_main returns */
