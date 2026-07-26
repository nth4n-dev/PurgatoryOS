/* src/kernel/shell.c */
#include <stdint.h>
#include <stddef.h>

#include "kernel/syscall.h"     /* sys_write / sys_read / sys_open / sys_close / sys_readdir */
#include "kernel/shell.h"

#define SH_LINEBUF_SIZE 128
#define SH_MAX_ARGS 8
#define SH_LOAD_ADDR  ((void *)0x0000000040210000UL)
#define SH_LOAD_SIZE  (64 * 1024)

/* Pure-computation helpers (no kernel calls) */

static uint64_t sh_strlen(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

static int sh_streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* I/O helpers. All go through SVC */

static void sh_puts(const char *s) {
    sys_write(1, s, sh_strlen(s));
}

static void sh_write(const char *buf, uint64_t len) {
    sys_write(1, buf, len);
}

/* Forward declaration (sh_dispatch calls sh_exec_flat before its definition) */
static int sh_exec_flat(const char *name);

static char sh_cwd[64] = "/home";

/* Resolve `path` against sh_cwd into `out` (absolute, normalized).
 * Handles relative paths, ".", and "..". Returns length, or -1 on overflow. */
static int sh_resolve_path(const char *path, char *out, int out_size) {
    char tmp[128];
    int n = 0;

    /* For relative paths, start from the current working directory. */
    if (path[0] != '/') {
        const char *p = sh_cwd;
        while (*p && n < (int)sizeof(tmp) - 1) tmp[n++] = *p++;
        if (n == 0 || tmp[n - 1] != '/') {
            if (n < (int)sizeof(tmp) - 1) tmp[n++] = '/';
        }
    }

    /* Append the input path. */
    while (*path && n < (int)sizeof(tmp) - 1) tmp[n++] = *path++;
    tmp[n] = '\0';

    /* Normalize components into `out`, handling "." and "..". */
    int opos = 0;
    out[opos++] = '/';
    const char *p = tmp;
    while (*p == '/') p++;        /* skip leading slashes */

    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        int clen = (int)(p - start);
        while (*p == '/') p++;    /* skip repeated slashes */

        if (clen == 0 || (clen == 1 && start[0] == '.')) continue;

        if (clen == 2 && start[0] == '.' && start[1] == '.') {
            /* Go up: strip the last path component from out. */
            if (opos > 1) {
                opos--;
                while (opos > 1 && out[opos - 1] != '/') opos--;
            }
            continue;
        }

        /* Append separator (skip for root). */
        if (opos > 1) {
            if (opos >= out_size - 1) return -1;
            out[opos++] = '/';
        }
        for (int i = 0; i < clen; i++) {
            if (opos >= out_size - 1) return -1;
            out[opos++] = start[i];
        }
    }

    out[opos] = '\0';
    return opos;
}

/* Line editor */

static int sh_readline(char *buf, int max) {
    int pos = 0;

    for (;;) {
        char c;
        sys_read(0, &c, 1);             /* blocking read from stdin (UART) */

        if (c == '\r' || c == '\n') {
            sys_write(1, "\n", 1);
            return pos;
        }

        if (c == 0x08 || c == 0x7f) {  /* backspace / DEL */
            if (pos > 0) {
                pos--;
                sys_write(1, "\b \b", 3);
            }
            continue;
        }

        if (c >= 0x20 && c <= 0x7e) {  /* printable ASCII */
            if (pos < max - 1) {
                buf[pos++] = c;
                sys_write(1, &c, 1);   /* local echo */
            }
            continue;
        }
        /* Silently drop control characters, arrow keys, etc. */
    }
}

/* Tokeniser */

static int sh_tokenise(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;

    while (*p && argc < SH_MAX_ARGS) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    return argc;
}

/* Built-in command prototypes */

typedef int (*sh_builtin_fn)(int argc, char *argv[]);

static int sh_cmd_help (int argc, char *argv[]);
static int sh_cmd_ls   (int argc, char *argv[]);
static int sh_cmd_cat  (int argc, char *argv[]);
static int sh_cmd_echo (int argc, char *argv[]);
static int sh_cmd_pwd  (int argc, char *argv[]);
static int sh_cmd_cd   (int argc, char *argv[]);
static int sh_cmd_clear(int argc, char *argv[]);

static const struct {
    const char    *name;
    sh_builtin_fn  fn;
} sh_builtins[] = {
    { "help",  sh_cmd_help  },
    { "ls",    sh_cmd_ls    },
    { "cat",   sh_cmd_cat   },
    { "echo",  sh_cmd_echo  },
    { "pwd",   sh_cmd_pwd   },
    { "cd",    sh_cmd_cd    },
    { "clear", sh_cmd_clear },
    { NULL,    NULL         },  /* sentinel */
};

/* Dispatcher */

static void sh_dispatch(int argc, char *argv[]) {
    if (argc == 0) return;

    for (int i = 0; sh_builtins[i].name != NULL; i++) {
        if (sh_streq(argv[0], sh_builtins[i].name)) {
            sh_builtins[i].fn(argc, argv);
            return;
        }
    }

    if (sh_exec_flat(argv[0]) == 0) return;

    sh_puts("sh: ");
    sh_puts(argv[0]);
    sh_puts(": command not found\n");
}

/* Built-in implementations */

static int sh_cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sh_puts("Built-in commands:\n");
    sh_puts("  help   - show this message\n");
    sh_puts("  ls     - list directory\n");
    sh_puts("  cat    - print file\n");
    sh_puts("  echo   - print arguments\n");
    sh_puts("  pwd    - print working directory\n");
    sh_puts("  cd     - change directory\n");
    sh_puts("  clear  - clear the screen\n");
    return 0;
}

static int sh_cmd_ls(int argc, char *argv[]) {
    char resolved[128];
    const char *arg = (argc > 1) ? argv[1] : sh_cwd;
    if (sh_resolve_path(arg, resolved, sizeof(resolved)) < 0) {
        sh_puts("ls: path too long\n"); return -1;
    }
    int64_t fd = sys_open(resolved, sh_strlen(resolved));
    if (fd < 0) {
        sh_puts("ls: "); sh_puts(resolved); sh_puts(": No such file or directory\n");
        return -1;
    }

    char buf[256];
    int64_t n = sys_readdir(fd, buf, sizeof(buf));
    sys_close(fd);
    if (n < 0) { sh_puts("ls: not a directory\n"); return -1; }

    int off = 0, emitted = 0;
    while (off < (int)sizeof(buf) && emitted < (int)n) {
        sh_puts(&buf[off]);
        sh_puts("  ");
        off += (int)sh_strlen(&buf[off]) + 1;
        emitted++;
    }
    sh_puts("\n");
    return 0;
}

static int sh_cmd_cat(int argc, char *argv[]) {
    if (argc < 2) { sh_puts("usage: cat <file>\n"); return -1; }
    char resolved[128];
    if (sh_resolve_path(argv[1], resolved, sizeof(resolved)) < 0) {
        sh_puts("cat: path too long\n"); return -1;
    }
    int64_t fd = sys_open(resolved, sh_strlen(resolved));
    if (fd < 0) {
        sh_puts("cat: "); sh_puts(resolved); sh_puts(": No such file or directory\n");
        return -1;
    }
    char buf[256];
    int64_t n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
        sh_write(buf, (uint64_t)n);
    sys_close(fd);
    return 0;
}

static int sh_cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) sh_puts(" ");
        sh_puts(argv[i]);
    }
    sh_puts("\n");
    return 0;
}

static int sh_cmd_pwd(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sh_puts(sh_cwd);
    sh_puts("\n");
    return 0;
}

static int sh_cmd_cd(int argc, char *argv[]) {
    const char *arg = (argc > 1) ? argv[1] : "/home";
    char resolved[64];
    if (sh_resolve_path(arg, resolved, sizeof(resolved)) < 0) {
        sh_puts("cd: path too long\n"); return -1;
    }
    int64_t fd = sys_open(resolved, sh_strlen(resolved));
    if (fd < 0) {
        sh_puts("cd: "); sh_puts(resolved); sh_puts(": No such file or directory\n");
        return -1;
    }
    sys_close(fd);
    uint64_t len = sh_strlen(resolved);
    for (uint64_t i = 0; i <= len; i++) sh_cwd[i] = resolved[i];
    return 0;
}

static int sh_cmd_clear(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sh_puts("\033[2J\033[H");
    return 0;
}

/* Flat-binary executor */

static int sh_exec_flat(const char *name) {
    char path[64];
    int pos = 0;
    const char *p = "/bin/";
    while (*p) path[pos++] = *p++;
    p = name;
    while (*p) path[pos++] = *p++;

    int64_t fd = sys_open(path, (uint64_t)pos);
    if (fd < 0) return -1;

    int64_t n = sys_read(fd, (char *)SH_LOAD_ADDR, SH_LOAD_SIZE);
    sys_close(fd);
    if (n <= 0) { sh_puts("sh: empty binary\n"); return 0; }

    char first = ((char *)SH_LOAD_ADDR)[0];
    if (first >= 0x20 && first < 0x7f) {
        /* ASCII content (e.g. /bin/hello). Print it directly. */
        sh_write((char *)SH_LOAD_ADDR, (uint64_t)n);
        return 0;
    }

    /* Real flat binary. Branch to it. */
    void (*entry)(void) = (void (*)(void))SH_LOAD_ADDR;
    entry();
    return 0;
}

/* Entry point (runs at EL0) */

void shell_main(void) {
    char line[SH_LINEBUF_SIZE];
    char *argv[SH_MAX_ARGS];

    sh_puts("\nWelcome to PurgatoryOS 0.13.\n");
    sh_puts("Type `help` for built-ins, or /bin/hello for the payoff.\n\n");

    for (;;) {
        sh_puts("root@purgatory:");
        sh_puts(sh_cwd);
        sh_puts("$ ");

        int n = sh_readline(line, SH_LINEBUF_SIZE);
        line[n] = '\0';

        int argc = sh_tokenise(line, argv);
        sh_dispatch(argc, argv);
    }
}
