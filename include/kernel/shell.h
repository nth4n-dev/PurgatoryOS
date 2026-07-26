/* include/kernel/shell.h
 *
 * Public interface to the in-kernel REPL.
 *
 * Everything else about the shell stays private to kernel/shell.c.
 * That includes the line discipline, the tokeniser and the built-in table.
 *
 * From Silicon to Shell, Post 13: Building the Shell
 */
#ifndef KERNEL_SHELL_H
#define KERNEL_SHELL_H

/*
 * shell_main is the entry point for the shell task.
 *
 * kernel_main registers it as an EL0 task. It runs an endless read and
 * eval loop:
 *
 *   1. Read a line from stdin, which is sys_read on fd 0.
 *   2. Split that line into tokens.
 *   3. Dispatch it through the built-in table, or load it from /bin.
 *
 * It never returns.
 */
void shell_main(void);

#endif /* KERNEL_SHELL_H */
