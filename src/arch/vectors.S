/* src/arch/vectors.S */
.section .text.vectors
.global vectors

/*
* VBAR_EL1 must be 2048-byte aligned. The hardware adds a fixed
* byte offset to this address to find each exception entry.
* .balign 2048 ensures the label 'vectors' sits at a correctly
* aligned address even if .text.vectors ends up at an odd location.
*/
.balign 2048
vectors:

/*
* MACRO: VENTRY <label>
* Emits a single 128-byte vector table entry that branches to <label>.
* .balign 128 keeps each slot at the right offset within the table.
* Any remaining space in the 128-byte window is implicitly zero-filled
* by the assembler (unreachable padding after the branch).
*/
.macro VENTRY label
.balign 128
  b \label
.endm

/* ── Group 0: Current EL, SP_EL0 (not used in our kernel) ── */
VENTRY  el1t_sync           /* 0x000 */
VENTRY  el1t_irq            /* 0x080 */
VENTRY  el1t_fiq            /* 0x100 */
VENTRY  el1t_serror         /* 0x180 */

/* ── Group 1: Current EL, SP_EL1 (our normal kernel context) ── */
VENTRY  el1h_sync           /* 0x200: kernel fault */
VENTRY  el1h_irq            /* 0x280: timer, UART and GIC interrupts */
VENTRY  el1h_fiq            /* 0x300 */
VENTRY  el1h_serror         /* 0x380 */

/* ── Group 2: Lower EL, AArch64 (future user-space syscalls) ── */
VENTRY  el0_sync_64         /* 0x400 */
VENTRY  el0_irq_64          /* 0x480 */
VENTRY  el0_fiq_64          /* 0x500 */
VENTRY  el0_serror_64       /* 0x580 */

/* ── Group 3: Lower EL, AArch32 (not supported) ── */
VENTRY  el0_sync_32         /* 0x600 */
VENTRY  el0_irq_32          /* 0x680 */
VENTRY  el0_fiq_32          /* 0x700 */
VENTRY  el0_serror_32       /* 0x780 */

/*
* MACRO: SAVE_REGS
* Push all general-purpose registers onto the stack before calling C.
* We save x0 to x29 (30 registers, 240 bytes), plus x30 (the link
* register). The stack must remain 16-byte aligned at all times on
* AArch64. Using stp with a #-16 pre-decrement handles this in pairs.
*/
.macro SAVE_REGS
  stp x0,  x1,  [sp, #-16]!
  stp x2,  x3,  [sp, #-16]!
  stp x4,  x5,  [sp, #-16]!
  stp x6,  x7,  [sp, #-16]!
  stp x8,  x9,  [sp, #-16]!
  stp x10, x11, [sp, #-16]!
  stp x12, x13, [sp, #-16]!
  stp x14, x15, [sp, #-16]!
  stp x16, x17, [sp, #-16]!
  stp x18, x19, [sp, #-16]!
  stp x20, x21, [sp, #-16]!
  stp x22, x23, [sp, #-16]!
  stp x24, x25, [sp, #-16]!
  stp x26, x27, [sp, #-16]!
  stp x28, x29, [sp, #-16]!
  str x30,      [sp, #-8]!
.endm

.macro RESTORE_REGS
  ldr x30,      [sp], #8
  ldp x28, x29, [sp], #16
  ldp x26, x27, [sp], #16
  ldp x24, x25, [sp], #16
  ldp x22, x23, [sp], #16
  ldp x20, x21, [sp], #16
  ldp x18, x19, [sp], #16
  ldp x16, x17, [sp], #16
  ldp x14, x15, [sp], #16
  ldp x12, x13, [sp], #16
  ldp x10, x11, [sp], #16
  ldp x8,  x9,  [sp], #16
  ldp x6,  x7,  [sp], #16
  ldp x4,  x5,  [sp], #16
  ldp x2,  x3,  [sp], #16
  ldp x0,  x1,  [sp], #16
.endm

/* ── Concrete handler stubs ── */
el1h_sync:
  SAVE_REGS
  bl  sync_exception_handler
  RESTORE_REGS
  eret

el1h_irq:
  SAVE_REGS
  bl  irq_handler
  RESTORE_REGS
  eret

/* Every unimplemented entry spins. Each one is a bug we have not handled yet. */
el1t_sync:
el1t_irq:
el1t_fiq:
el1t_serror:
el1h_fiq:
el1h_serror:
el0_sync_64:
el0_irq_64:
el0_fiq_64:
el0_serror_64:
el0_sync_32:
el0_irq_32:
el0_fiq_32:
el0_serror_32:
  b .  /* spin, this is unrecoverable */
