/* user/user.S */
.section .text.user  /* Matches the linker script's .user_text block */

.global sys_write
.global sys_exit
.global sys_getpid
.global sys_yield

sys_write:
    mov x8, #1
    svc #0
    ret

sys_exit:
    mov x8, #2
    svc #0
    b .

sys_getpid:
    mov x8, #3
    svc #0
    ret

sys_yield:
    mov x8, #4
    svc #0
    ret
