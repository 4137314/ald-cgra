#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "compile.h"
#include "paths.h"

int cli_parse_ints(const char *text, int16_t *out, size_t cap, size_t *n)
{
    *n = 0;
    const char *p = text;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ','))
            p++;
        if (*p == '\0')
            break;
        char *end = NULL;
        errno = 0;
        long v = strtol(p, &end, 0);
        if (errno == ERANGE || v < INT16_MIN || v > UINT16_MAX ||
            (*end && *end != ',' && !isspace((unsigned char)*end)) || end == p)
            return -1;
        if (*n >= cap)
            return -1;
        out[(*n)++] = (int16_t)v;
        p = end;
    }
    return 0;
}

static char *slurp(FILE *f)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    size_t r;
    while ((r = fread(buf + len, 1, cap - len, f)) > 0) {
        len += r;
        if (len == cap) {
            if (cap > SIZE_MAX / 2) { free(buf); return NULL; }
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
    }
    if (ferror(f) || memchr(buf, 0, len)) { free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

/* Read a vector from an inline list, "@file", or "-" (stdin). */
int cli_read_vec(const char *src, int16_t *out, size_t cap, size_t *n)
{
    if (strcmp(src, "-") == 0) {
        char *buf = slurp(stdin);
        if (!buf) return -1;
        int rc = cli_parse_ints(buf, out, cap, n);
        free(buf);
        return rc;
    }
    if (src[0] == '@') {
        FILE *f = fopen(src + 1, "rb");
        if (!f) { fprintf(stderr, "cgra: cannot open %s: %s\n", src + 1, strerror(errno)); return -1; }
        char *buf = slurp(f);
        if (fclose(f) != 0) { free(buf); return -1; }
        if (!buf) return -1;
        int rc = cli_parse_ints(buf, out, cap, n);
        free(buf);
        return rc;
    }
    return cli_parse_ints(src, out, cap, n);
}

void cli_print_vec(cli_context *inv, const int16_t *v, size_t n, const dsl_io *io)
{
    FILE *out = inv->out ? inv->out : stdout;
    const char *fmt = inv->format ? inv->format
                    : (io && io->format[0] ? io->format : "dec");
    const char *sep = io && io->sep[0] ? io->sep : "ws";
    const char *between = strcmp(sep, "comma") == 0 ? ","
                        : strcmp(sep, "newline") == 0 ? "\n" : " ";

    if (strcmp(fmt, "json") == 0) {
        /* An object (not a bare array) so results compose with jq '.result'. */
        fprintf(out, "{\"count\": %zu, \"result\": [", n);
        for (size_t i = 0; i < n; i++) {
            if (i) fputs(", ", out);
            if (inv->unsigned_output) fprintf(out, "%u", (unsigned)(uint16_t)v[i]);
            else            fprintf(out, "%d", v[i]);
        }
        fputs("]}\n", out);
        return;
    }

    for (size_t i = 0; i < n; i++) {
        if (i) fputs(between, out);
        if (strcmp(fmt, "hex") == 0)
            fprintf(out, "0x%04X", (uint16_t)v[i]);
        else if (strcmp(fmt, "bin") == 0) {
            for (int b = 15; b >= 0; b--) fputc(((uint16_t)v[i] >> b) & 1 ? '1' : '0', out);
        } else if (inv->unsigned_output)
            fprintf(out, "%u", (unsigned)(uint16_t)v[i]);
        else
            fprintf(out, "%d", v[i]);
    }
    fputc('\n', out);
}
