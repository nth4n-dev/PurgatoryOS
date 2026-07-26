/* src/arch/boot.S */
.section .text.boot
.global _start
 
_start:
  mrs  x0, CurrentEL
  lsr  x0, x0, #2
  cmp  x0, #1
  beq  .el1_ready
 
  mov  x0, #(1 << 31)
  msr  hcr_el2, x0
 
  mov  x0, #0x3c5
  msr  spsr_el2, x0
 
  adr  x0, .el1_ready
  msr  elr_el2, x0
 
  eret
 
.el1_ready:
  ldr  x0, =_stack_top
  mov  sp, x0
 
  ldr  x1, =__bss_start
  ldr  x2, =__bss_end
.zero_bss:
  cmp  x1, x2
  bge  .call_kernel
  str  xzr, [x1], #8
  b    .zero_bss
 
.call_kernel:
  bl   kernel_main
  b    .