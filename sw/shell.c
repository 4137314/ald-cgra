#define _GNU_SOURCE
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cli.h"
#include "compile.h"
#include "paths.h"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

/* -------------------------------------------------- interactive shell */

#define SHELL_LINE_MAX 65536u
#define SHELL_TOKEN_MAX 32768u
#define SHELL_ARG_MAX 128

/*
 * Split a line into an argv[], honouring single/double quotes so that
 * `run add --a "1 2 3"` keeps the vector as one token. Tokens are compacted
 * in place (quotes stripped); argv[0] is the tool name. Returns argc.
 */
static int shell_tokenize(char *line, char **argv, int max)
{
    static char prog[] = "cgra";
    int argc = 0;
    argv[argc++] = prog;
    char *p = line;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc >= max - 1) {
            fprintf(stderr, "cgra: shell argument limit exceeded\n"); return -1;
        }
        char *dst = p;
        argv[argc++] = dst;
        char *start = dst;
        char quote = 0;
        while (*p && (quote || !isspace((unsigned char)*p))) {
            if (!quote && (*p == '"' || *p == '\'')) { quote = *p; p++; continue; }
            if (quote && *p == quote)                { quote = 0;  p++; continue; }
            *dst++ = *p++;
            if ((size_t)(dst - start) > SHELL_TOKEN_MAX) {
                fprintf(stderr, "cgra: shell token too long\n"); return -1;
            }
        }
        if (quote) { fprintf(stderr, "cgra: unclosed shell quote\n"); return -1; }
        if (*p) p++;               /* consume the separating space */
        *dst = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

/* Return 1 for a complete line, 0 for EOF, -1 for a rejected line. The
 * fallback drains oversized/binary lines; no prefix can reach dispatch. */
static int shell_readline(const char *prompt, char **result)
{
    *result = NULL;
#ifdef HAVE_READLINE
    if (isatty(STDIN_FILENO)) {
        char *line = readline(prompt);
        if (!line) return 0;
        if (strlen(line) > SHELL_LINE_MAX) {
            free(line); fprintf(stderr, "cgra: shell line too long\n"); return -1;
        }
        if (*line) add_history(line);
        *result = line;
        return 1;
    }
#endif
    char *line = malloc(SHELL_LINE_MAX + 1u);
    if (!line) { fprintf(stderr, "cgra: cannot allocate shell line\n"); return -2; }
    fputs(prompt, stdout);
    fflush(stdout);
    size_t len = 0;
    int ch, invalid = 0;
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (!ch) invalid = 1;
        if (len == SHELL_LINE_MAX) invalid = 1;
        else line[len++] = (char)ch;
    }
    if (ferror(stdin)) { free(line); fprintf(stderr, "cgra: shell input error\n"); return -2; }
    if (invalid) {
        free(line); fprintf(stderr, "cgra: shell line too long or contains NUL\n"); return -1;
    }
    if (!len && ch == EOF) { free(line); return 0; }
    line[len] = '\0';
    *result = line;
    return 1;
}

int cli_cmd_shell(char *opt_config)
{
    printf("cgra interactive shell — commands without the leading 'cgra'.\n"
           "  help    list commands        quit / exit / Ctrl-D    leave\n");
    if (opt_config)
        printf("  (config: %s)\n", opt_config);

    int last = 0;
    for (;;) {
        char *line;
        int read_status = shell_readline("cgra> ", &line);
        if (!read_status) { putchar('\n'); break; }
        if (read_status == -2) { last = 1; break; }
        if (read_status < 0) { last = 1; continue; }

        char *argv[SHELL_ARG_MAX];
        int argc = shell_tokenize(line, argv, SHELL_ARG_MAX);
        if (argc < 0) { last = 1; free(line); continue; }
        if (argc <= 1) { free(line); continue; }     /* empty line */

        const char *w = argv[1];
        if (!strcmp(w, "quit") || !strcmp(w, "exit") || !strcmp(w, "q")) {
            if (argc == 2) { free(line); break; }
            fprintf(stderr, "cgra: quit takes no arguments\n"); last = 1; free(line); continue;
        }
        if (!strcmp(w, "help") || !strcmp(w, "?")) {
            if (argc == 2) { cli_usage(stdout); last = 0; }
            else { fprintf(stderr, "cgra: help takes no arguments\n"); last = 1; }
            free(line); continue;
        }

        /* Inject the inherited option first; a later explicit spelling wins.
         * Reject overflow instead of silently dropping inherited config. */
        if (opt_config) {
            if (argc > SHELL_ARG_MAX - 3) {
                fprintf(stderr, "cgra: shell argument limit exceeded by inherited config\n");
                last = 1; free(line); continue;
            }
            /* splice "-c <file>" right after argv[0] */
            static char dashc[] = "-c";
            for (int i = argc; i >= 1; i--) argv[i + 2] = argv[i];
            argv[1] = dashc;
            argv[2] = opt_config;
            argc += 2;
        }

        last = cli_dispatch(argc, argv, 1);
        free(line);
    }
    return last;
}
