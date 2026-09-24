/*
 * dsl.c — parser and discovery for the .cgra configuration language.
 */

#define _POSIX_C_SOURCE 200809L

#include "dsl.h"
#include "paths.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* -------------------------------------------------- small helpers */

int dsl_integer(const char *text, long lo, long hi, long *value)
{
    if (!text || !*text || !value) return -1;
    char *end;
    errno = 0;
    long v = strtol(text, &end, 0);
    if (errno == ERANGE || end == text || *end || v < lo || v > hi) return -1;
    *value = v;
    return 0;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';
    return s;
}

/* Remove a trailing '#' comment; a leading ';' marks a whole-line comment. */
static void strip_comment(char *s)
{
    char *h = strchr(s, '#');
    if (h)
        *h = '\0';
    char *t = s;
    while (*t && isspace((unsigned char)*t))
        t++;
    if (*t == ';')
        *t = '\0';
}

static int copy_field(char *dst, size_t dstsz, const char *src)
{
    size_t n = strlen(src);
    if (n >= dstsz) return -1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}

/* Coordinate grammar: two complete, bounded integers separated by a comma. */
int dsl_coordinate(const char *text, int *row, int *col)
{
    char *end;
    errno = 0;
    long r = strtol(text, &end, 0);
    if (errno == ERANGE || end == text || r < 0 || r >= CGRA_MAX_EDGE) return -1;
    while (isspace((unsigned char)*end)) end++;
    if (*end++ != ',') return -1;
    const char *start = end;
    errno = 0;
    long c = strtol(start, &end, 0);
    if (errno == ERANGE || end == start || c < 0 || c >= CGRA_MAX_EDGE) return -1;
    while (isspace((unsigned char)*end)) end++;
    if (*end) return -1;
    *row = (int)r; *col = (int)c;
    return 0;
}

/* -------------------------------------------------- model construction */

void dsl_init(dsl_ctx *d)
{
    memset(d, 0, sizeof(*d));
}

dsl_device *dsl_find_device(dsl_ctx *d, const char *name)
{
    for (int i = 0; i < d->ndev; i++)
        if (strcmp(d->dev[i].name, name) == 0)
            return &d->dev[i];
    return NULL;
}

dsl_mode *dsl_find_mode(dsl_ctx *d, const char *name)
{
    for (int i = 0; i < d->nmode; i++)
        if (strcmp(d->mode[i].name, name) == 0)
            return &d->mode[i];
    return NULL;
}

dsl_pipeline *dsl_find_pipeline(dsl_ctx *d, const char *name)
{
    for (int i = 0; i < d->npipe; i++)
        if (strcmp(d->pipe[i].name, name) == 0)
            return &d->pipe[i];
    return NULL;
}

dsl_io *dsl_find_io(dsl_ctx *d, const char *name)
{
    for (int i = 0; i < d->nio; i++)
        if (strcmp(d->io[i].name, name) == 0)
            return &d->io[i];
    return NULL;
}

static dsl_device *device_get(dsl_ctx *d, const char *name)
{
    dsl_device *e = dsl_find_device(d, name);
    if (e)
        return e;
    if (d->ndev >= DSL_MAX_DEV)
        return NULL;
    e = &d->dev[d->ndev++];
    memset(e, 0, sizeof(*e));
    e->baud = 115200; e->timeout_ms = 2000;
    copy_field(e->name, sizeof(e->name), name);
    return e;
}

static dsl_mode *mode_get(dsl_ctx *d, const char *name)
{
    dsl_mode *e = dsl_find_mode(d, name);
    if (e)
        return e;
    if (d->nmode >= DSL_MAX_MODE)
        return NULL;
    e = &d->mode[d->nmode++];
    memset(e, 0, sizeof(*e));
    copy_field(e->name, sizeof(e->name), name);
    return e;
}

static dsl_pipeline *pipeline_get(dsl_ctx *d, const char *name)
{
    dsl_pipeline *e = dsl_find_pipeline(d, name);
    if (e)
        return e;
    if (d->npipe >= DSL_MAX_PIPE)
        return NULL;
    e = &d->pipe[d->npipe++];
    memset(e, 0, sizeof(*e));
    copy_field(e->name, sizeof(e->name), name);
    return e;
}

static dsl_io *io_get(dsl_ctx *d, const char *name)
{
    dsl_io *e = dsl_find_io(d, name);
    if (e)
        return e;
    if (d->nio >= DSL_MAX_IO)
        return NULL;
    e = &d->io[d->nio++];
    memset(e, 0, sizeof(*e));
    copy_field(e->name, sizeof(e->name), name);
    return e;
}

static const char *var_lookup(dsl_ctx *d, const char *name)
{
    for (int i = 0; i < d->nvar; i++)
        if (strcmp(d->var[i].name, name) == 0)
            return d->var[i].val;
    return NULL;
}

static int var_set(dsl_ctx *d, const char *name, const char *val)
{
    for (int i = 0; i < d->nvar; i++)
        if (strcmp(d->var[i].name, name) == 0)
            return copy_field(d->var[i].val, sizeof(d->var[i].val), val);
    if (d->nvar >= DSL_MAX_VARS) return -1;
    if (copy_field(d->var[d->nvar].name, DSL_NAME, name) != 0 ||
        copy_field(d->var[d->nvar].val, DSL_VAL, val) != 0) return -1;
    d->nvar++;
    return 0;
}

/* Expand defined $NAME / ${NAME}; $$ emits a literal dollar sign.
 * Reject malformed/unknown references and output that would be truncated. */
static int subst_vars(dsl_ctx *d, const char *in, char *out, size_t outsz)
{
    size_t o = 0;
    for (size_t i = 0; in[i]; ) {
        if (in[i] == '$' && in[i + 1] != '$') {
            int braced = in[i + 1] == '{';
            size_t start = i + 1 + (size_t)braced, end = start;
            if (!isalpha((unsigned char)in[end]) && in[end] != '_') return -1;
            while (isalnum((unsigned char)in[end]) || in[end] == '_') end++;
            if (end - start >= DSL_NAME || (braced && in[end] != '}')) return -1;
            char name[DSL_NAME];
            memcpy(name, in + start, end - start); name[end - start] = '\0';
            const char *value = var_lookup(d, name);
            if (!value || strlen(value) >= outsz - o) return -1;
            memcpy(out + o, value, strlen(value)); o += strlen(value);
            i = end + (size_t)braced;
        } else {
            if (o + 1 >= outsz) return -1;
            out[o++] = in[i];
            i += (in[i] == '$' && in[i + 1] == '$') ? 2 : 1;
        }
    }
    out[o] = '\0';
    return 0;
}

/* -------------------------------------------------- directive handlers */

enum sect { SECT_NONE, SECT_DEVICE, SECT_MODE, SECT_PIPE, SECT_IO };

static dsl_pe *mode_pe_get(dsl_mode *m, int r, int c)
{
    for (int i = 0; i < m->npe; i++)
        if (m->pe[i].r == r && m->pe[i].c == c)
            return &m->pe[i];
    if (m->npe >= CGRA_MAX_PE)
        return NULL;
    dsl_pe *p = &m->pe[m->npe++];
    memset(p, 0, sizeof(*p));
    p->r = r;
    p->c = c;
    return p;
}

/* Fill a dsl_pe from "op=mac a=north b=west". */
static int parse_pe_value(dsl_pe *p, const char *value)
{
    if (!*value) return -1;
    char tmp[DSL_VAL];
    if (copy_field(tmp, sizeof(tmp), value) != 0) return -1;
    char *save = NULL;
    unsigned seen = 0;
    for (char *tok = strtok_r(tmp, " \t", &save); tok;
         tok = strtok_r(NULL, " \t", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq || !eq[1]) return -1;
        *eq = '\0';
        const char *k = tok, *v = eq + 1;
        unsigned field = !strcmp(k, "op") ? 1u : !strcmp(k, "a") ? 2u : !strcmp(k, "b") ? 4u : 0u;
        if (!field || (seen & field)) return -1;
        seen |= field;
        if (strcmp(k, "op") == 0) { if (copy_field(p->op, sizeof(p->op), v)) return -1; }
        else if (strcmp(k, "a") == 0) { if (copy_field(p->a, sizeof(p->a), v)) return -1; }
        else if (strcmp(k, "b") == 0) { if (copy_field(p->b, sizeof(p->b), v)) return -1; }
        else return -1;
    }
    return 0;
}

static int apply_directive(dsl_ctx *d, enum sect sect, void *cur,
                           const char *key, const char *value,
                           const char *origin, int lineno,
                           char *err, size_t errsz)
{
    (void)d;
    long number;
#define COPY(field) do { if (copy_field((field), sizeof(field), value)) goto invalid; } while (0)
    /* 'doc' is accepted (and ignored) in sections without a doc field. */
    if (strcmp(key, "doc") == 0 && (sect == SECT_DEVICE || sect == SECT_IO))
        return 0;

    switch (sect) {
    case SECT_DEVICE: {
        dsl_device *e = cur;
        if (strcmp(key, "port") == 0)         COPY(e->port);
        else if (strcmp(key, "baud") == 0)    {
            if (dsl_integer(value, 1, INT_MAX, &number)) goto invalid;
            e->baud = (int)number;
        }
        else if (strcmp(key, "timeout") == 0) {
            if (dsl_integer(value, 0, INT_MAX, &number)) goto invalid;
            e->timeout_ms = (int)number;
        }
        else if (strcmp(key, "board") == 0)   COPY(e->board);
        else goto unknown;
        return 0;
    }
    case SECT_MODE: {
        dsl_mode *e = cur;
        if (strncmp(key, "pe ", 3) == 0 || strncmp(key, "pe\t", 3) == 0) {
            int r = -1, c = -1;
            if (dsl_coordinate(key + 3, &r, &c) != 0) {
                snprintf(err, errsz, "%s:%d: bad PE coordinate '%s'", origin, lineno, key);
                return -1;
            }
            dsl_pe *p = mode_pe_get(e, r, c);
            if (!p) {
                snprintf(err, errsz, "%s:%d: too many PEs", origin, lineno);
                return -1;
            }
            if (parse_pe_value(p, value) != 0) {
                snprintf(err, errsz, "%s:%d: expected PE fields op=, a=, b=", origin, lineno);
                return -1;
            }
            return 0;
        }
        if (strcmp(key, "doc") == 0)          COPY(e->doc);
        else if (strcmp(key, "pattern") == 0) COPY(e->pattern);
        else if (strcmp(key, "op") == 0)      COPY(e->op);
        else if (strcmp(key, "a") == 0)       COPY(e->a);
        else if (strcmp(key, "b") == 0)       COPY(e->b);
        else if (strcmp(key, "steps") == 0)   COPY(e->steps);
        else if (strcmp(key, "out") == 0)     COPY(e->out);
        else if (strcmp(key, "reset") == 0)   COPY(e->reset);
        else goto unknown;
        return 0;
    }
    case SECT_PIPE: {
        dsl_pipeline *e = cur;
        if (strcmp(key, "doc") == 0) {
            COPY(e->doc);
            return 0;
        }
        if (strcmp(key, "stage") == 0) {
            if (e->nstage >= DSL_MAX_STAGE) {
                snprintf(err, errsz, "%s:%d: too many stages", origin, lineno);
                return -1;
            }
            dsl_stage *st = &e->stage[e->nstage++];
            memset(st, 0, sizeof(*st));
            char tmp[DSL_VAL];
            if (copy_field(tmp, sizeof(tmp), value)) goto invalid;
            char *save = NULL;
            char *first = strtok_r(tmp, " \t", &save);
            if (!first) {
                snprintf(err, errsz, "%s:%d: stage needs a mode", origin, lineno);
                return -1;
            }
            if (copy_field(st->mode, sizeof(st->mode), first)) goto invalid;
            for (char *tok = strtok_r(NULL, " \t", &save); tok;
                 tok = strtok_r(NULL, " \t", &save)) {
                if (st->has_imm || strncmp(tok, "imm=", 4) != 0 ||
                    dsl_integer(tok + 4, INT16_MIN, UINT16_MAX, &st->imm) != 0) {
                    snprintf(err, errsz, "%s:%d: invalid stage option '%s'", origin, lineno, tok);
                    return -1;
                }
                st->has_imm = 1;
            }
            return 0;
        }
        goto unknown;
    }
    case SECT_IO: {
        dsl_io *e = cur;
        if (!strcmp(key, "format") && strcmp(value, "dec") && strcmp(value, "hex") &&
            strcmp(value, "bin") && strcmp(value, "json")) goto invalid;
        if (!strcmp(key, "sep") && strcmp(value, "ws") && strcmp(value, "comma") &&
            strcmp(value, "newline")) goto invalid;
        if (strcmp(key, "format") == 0)     COPY(e->format);
        else if (strcmp(key, "width") == 0) {
            if (dsl_integer(value, CGRA_DATA_W, CGRA_DATA_W, &number)) goto invalid;
            e->width = (int)number;
        }
        else if (strcmp(key, "sep") == 0)   COPY(e->sep);
        else goto unknown;
        return 0;
    }
    default:
        snprintf(err, errsz, "%s:%d: directive '%s' outside any section",
                 origin, lineno, key);
        return -1;
    }

#undef COPY
invalid:
    snprintf(err, errsz, "%s:%d: invalid or oversized value for '%s'", origin, lineno, key);
    return -1;
unknown:
    snprintf(err, errsz, "%s:%d: unknown directive '%s'", origin, lineno, key);
    return -1;
}

/* -------------------------------------------------- parser */

static int g_include_depth = 0;

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Resolve an include 'path' by searching, in order: the directory of the
 * including file, each CGRA_PATH entry, the user config dir and its stdlib/,
 * then the system stdlib and /etc/cgra. Returns 1 on success. Absolute paths
 * are used verbatim. */
static int try_root(const char *root, const char *sub, const char *path,
                    char *out, size_t outsz)
{
    char cand[2 * DSL_VAL];
    int length = sub ? snprintf(cand, sizeof(cand), "%s/%s/%s", root, sub, path)
                     : snprintf(cand, sizeof(cand), "%s/%s", root, path);
    if (length < 0 || (size_t)length >= sizeof(cand)) return 0;
    if (!file_exists(cand))
        return 0;
    return copy_field(out, outsz, cand) == 0;
}

static int find_include(const char *origin, const char *path,
                        char *out, size_t outsz)
{
    if (path[0] == '/') {
        return copy_field(out, outsz, path) == 0 && file_exists(out);
    }

    /* 1. relative to the including file */
    const char *slash = strrchr(origin, '/');
    if (slash) {
        char cand[2 * DSL_VAL];
        int length = snprintf(cand, sizeof(cand), "%.*s/%s", (int)(slash - origin), origin, path);
        if (length >= 0 && (size_t)length < sizeof(cand) && file_exists(cand))
            return copy_field(out, outsz, cand) == 0;
    }

    /* 2. CGRA_PATH entries */
    const char *cp = getenv("CGRA_PATH");
    if (cp && *cp) {
        char *tmp = strdup(cp);
        if (!tmp) return 0;
        int found = 0;
        char *save = NULL;
        for (char *dir = strtok_r(tmp, ":", &save); dir; dir = strtok_r(NULL, ":", &save))
            if (try_root(dir, NULL, path, out, outsz)) { found = 1; break; }
        free(tmp);
        if (found) return 1;
    }

    /* 3. user config dir and its stdlib/ */
    char udir[DSL_VAL];
    if (dsl_user_dir(udir, sizeof(udir)) == 0) {
        if (try_root(udir, NULL, path, out, outsz)) return 1;
        if (try_root(udir, "stdlib", path, out, outsz)) return 1;
    }

    /* 4. system locations */
    if (try_root(CGRA_STDLIB_DIR, NULL, path, out, outsz)) return 1;
    if (try_root("/etc/cgra", NULL, path, out, outsz)) return 1;

    return 0;
}

static int parse_into(dsl_ctx *d, const char *text, const char *origin,
                      char *err, size_t errsz)
{
    char errbuf[DSL_VAL];
    if (!err) { err = errbuf; errsz = sizeof(errbuf); }
    if (errsz) err[0] = '\0';

    enum sect sect = SECT_NONE;
    void *cur = NULL;

    char *copy = strdup(text);
    if (!copy) {
        snprintf(err, errsz, "%s: out of memory", origin);
        return -1;
    }

    int lineno = 0, rc = 0;
    char *next = copy;
    while (next) {
        char *line = next;
        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        lineno++;
        strip_comment(line);
        char *s = trim(line);
        if (*s == '\0')
            continue;

        /* include DIRECTIVE: pull in another .cgra file, searched across the
         * include roots (relative, CGRA_PATH, stdlib, /etc). */
        if (strncmp(s, "include", 7) == 0 && (s[7] == ' ' || s[7] == '\t')) {
            char *path = trim(s + 7);
            if (path[0] == '"' || path[0] == '\'') {
                if (strlen(path) < 2 || path[strlen(path) - 1] != path[0]) goto syntax;
                path[strlen(path) - 1] = '\0';
                path++;
            }
            if (!*path || strlen(path) >= DSL_VAL) goto syntax;
            if (g_include_depth >= 8) {
                snprintf(err, errsz, "%s:%d: include nesting too deep", origin, lineno);
                rc = -1;
                break;
            }
            char resolved[DSL_VAL];
            if (!find_include(origin, path, resolved, sizeof(resolved))) {
                snprintf(err, errsz, "%s:%d: include '%s' not found", origin, lineno, path);
                rc = -1;
                break;
            }
            g_include_depth++;
            int irc = dsl_parse_file(d, resolved, err, errsz);
            g_include_depth--;
            if (irc != 0) { rc = -1; break; }
            continue;
        }

        /* set NAME = value  (or: set NAME value) — defines a $variable. */
        if (strncmp(s, "set", 3) == 0 && (s[3] == ' ' || s[3] == '\t')) {
            char *rest = trim(s + 3);
            char name[DSL_NAME] = {0};
            if (!isalpha((unsigned char)*rest) && *rest != '_') goto syntax;
            size_t k = 0;
            while (isalnum((unsigned char)rest[k]) || rest[k] == '_') k++;
            if (k >= sizeof(name) || (rest[k] && rest[k] != '=' &&
                !isspace((unsigned char)rest[k]))) goto syntax;
            memcpy(name, rest, k);
            char *v = trim(rest + k);
            if (*v == '=') v = trim(v + 1);
            char expanded[DSL_VAL];
            if (subst_vars(d, v, expanded, sizeof(expanded)) || var_set(d, name, expanded))
                goto syntax;
            continue;
        }

        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close) {
                snprintf(err, errsz, "%s:%d: missing ']'", origin, lineno);
                rc = -1;
                break;
            }
            if (*trim(close + 1)) goto syntax;
            *close = '\0';
            char *hdr = trim(s + 1);
            char *save = NULL;
            char *type = strtok_r(hdr, " \t\r", &save);
            char *name = strtok_r(NULL, " \t\r", &save);
            if (!type || !name || strlen(type) >= DSL_TOK || strlen(name) >= DSL_NAME ||
                strtok_r(NULL, " \t\r", &save)) goto syntax;
            for (const char *p = name; *p; p++)
                if (iscntrl((unsigned char)*p)) goto syntax;
            if (strcmp(type, "device") == 0)        { sect = SECT_DEVICE; cur = device_get(d, name); }
            else if (strcmp(type, "mode") == 0)     { sect = SECT_MODE;   cur = mode_get(d, name); }
            else if (strcmp(type, "pipeline") == 0) { sect = SECT_PIPE;   cur = pipeline_get(d, name); }
            else if (strcmp(type, "io") == 0)       { sect = SECT_IO;     cur = io_get(d, name); }
            else {
                snprintf(err, errsz, "%s:%d: unknown section type '%s'", origin, lineno, type);
                rc = -1;
                break;
            }
            if (!cur) {
                snprintf(err, errsz, "%s:%d: too many '%s' sections", origin, lineno, type);
                rc = -1;
                break;
            }
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            snprintf(err, errsz, "%s:%d: expected 'key = value'", origin, lineno);
            rc = -1;
            break;
        }
        *eq = '\0';
        char *key = trim(s);
        char *value = trim(eq + 1);
        char expanded[DSL_VAL];
        if (subst_vars(d, value, expanded, sizeof(expanded))) goto syntax;
        if (apply_directive(d, sect, cur, key, expanded, origin, lineno, err, errsz) != 0) {
            rc = -1;
            break;
        }
    }

    free(copy);
    return rc;
syntax:
    snprintf(err, errsz, "%s:%d: malformed/oversized token or undefined variable", origin, lineno);
    free(copy);
    return -1;
}

int dsl_parse(dsl_ctx *d, const char *text, const char *origin,
              char *err, size_t errsz)
{
    if (!d || !text || !origin) return -1;
    dsl_ctx *candidate = malloc(sizeof(*candidate));
    if (!candidate) {
        if (err) snprintf(err, errsz, "%s: out of memory", origin);
        return -1;
    }
    *candidate = *d;
    int rc = parse_into(candidate, text, origin, err, errsz);
    if (rc == 0) *d = *candidate;
    free(candidate);
    return rc;
}

int dsl_parse_file(dsl_ctx *d, const char *path, char *err, size_t errsz)
{
    if (!d || !path || strlen(path) >= DSL_VAL) {
        if (err) snprintf(err, errsz, "invalid configuration path (maximum %d bytes)", DSL_VAL - 1);
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) snprintf(err, errsz, "cannot open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        if (err) snprintf(err, errsz, "cannot seek %s", path);
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0 || (uintmax_t)sz >= SIZE_MAX || fseek(f, 0, SEEK_SET) != 0) {
        if (err) snprintf(err, errsz, "cannot size/read %s", path);
        fclose(f);
        return -1;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        if (err) snprintf(err, errsz, "%s: out of memory", path);
        fclose(f); return -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    int failed = ferror(f) || n != (size_t)sz || memchr(buf, 0, n) != NULL;
    buf[n] = '\0';
    if (fclose(f) != 0) failed = 1;
    if (failed) {
        if (err) snprintf(err, errsz, "cannot read complete configuration %s", path);
        free(buf);
        return -1;
    }

    dsl_ctx *candidate = malloc(sizeof(*candidate));
    if (!candidate || d->npath >= DSL_MAX_PATHS) {
        if (err) snprintf(err, errsz, "%s: out of memory or too many configuration files", path);
        free(candidate); free(buf); return -1;
    }
    *candidate = *d;
    copy_field(candidate->paths[candidate->npath++], DSL_VAL, path);
    int rc = parse_into(candidate, buf, path, err, errsz);
    if (rc == 0) *d = *candidate;
    free(candidate);
    free(buf);
    return rc;
}

/* -------------------------------------------------- discovery */

int dsl_user_dir(char *buf, size_t bufsz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        int n = snprintf(buf, bufsz, "%s/cgra", xdg);
        return n < 0 || (size_t)n >= bufsz ? -2 : 0;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        int n = snprintf(buf, bufsz, "%s/.config/cgra", home);
        return n < 0 || (size_t)n >= bufsz ? -2 : 0;
    }
    return -1;
}

static int ends_with_cgra(const char *name)
{
    size_t n = strlen(name);
    return n > 5 && strcmp(name + n - 5, ".cgra") == 0;
}

/* Parse config.cgra first (if present), then the remaining *.cgra sorted. */
static int load_dir(dsl_ctx *d, const char *dir, char *err, size_t errsz)
{
    if (strlen(dir) >= DSL_VAL) {
        if (err) snprintf(err, errsz, "configuration directory exceeds %d bytes", DSL_VAL - 1);
        return -1;
    }
    struct stat stbuf;
    if (stat(dir, &stbuf) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return 0;
        if (err) snprintf(err, errsz, "cannot inspect %s: %s", dir, strerror(errno));
        return -1;
    }
    if (!S_ISDIR(stbuf.st_mode)) {
        if (err) snprintf(err, errsz, "%s is not a configuration directory", dir);
        return -1;
    }

    char path[DSL_VAL + 260];
    snprintf(path, sizeof(path), "%s/config.cgra", dir);
    if (stat(path, &stbuf) == 0) {
        if (dsl_parse_file(d, path, err, errsz) != 0) return -1;
    } else if (errno != ENOENT) {
        if (err) snprintf(err, errsz, "cannot inspect %s: %s", path, strerror(errno));
        return -1;
    }

    struct dirent **names = NULL;
    int n = scandir(dir, &names, NULL, alphasort);
    if (n < 0) {
        if (err) snprintf(err, errsz, "cannot list %s: %s", dir, strerror(errno));
        return -1;
    }
    int rc = 0;
    for (int i = 0; i < n; i++) {
        if (rc == 0 && ends_with_cgra(names[i]->d_name) &&
            strcmp(names[i]->d_name, "config.cgra") != 0) {
            snprintf(path, sizeof(path), "%s/%s", dir, names[i]->d_name);
            rc = dsl_parse_file(d, path, err, errsz);
        }
        free(names[i]);
    }
    free(names);
    return rc;
}

int dsl_load_defaults(dsl_ctx *d, char *err, size_t errsz)
{
    return dsl_parse(d, dsl_default_text(), "<builtin>", err, errsz);
}

/* Load a directory's top-level *.cgra, then its conf.d/ drop-ins (alphabetical,
 * so 99_*.cgra overrides 01_*.cgra by name). */
static int load_tree(dsl_ctx *d, const char *dir, char *err, size_t errsz)
{
    char confd[DSL_VAL + 16];
    if (load_dir(d, dir, err, errsz) != 0) return -1;
    snprintf(confd, sizeof(confd), "%s/conf.d", dir);
    return load_dir(d, confd, err, errsz);
}

int dsl_load(dsl_ctx *d, const char *extra, char *err, size_t errsz)
{
    if (dsl_load_defaults(d, err, errsz) != 0)
        return -1;

    if (load_tree(d, "/etc/cgra", err, errsz) != 0) return -1;

    char udir[DSL_VAL];
    int user_dir = dsl_user_dir(udir, sizeof(udir));
    if (user_dir == -2) {
        if (err) snprintf(err, errsz, "user configuration directory is too long");
        return -1;
    }
    if (user_dir == 0 && load_tree(d, udir, err, errsz) != 0) return -1;

    /* CGRA_PATH: extra config roots (like PATH), colon-separated. */
    const char *cp = getenv("CGRA_PATH");
    if (cp && *cp) {
        char *tmp = strdup(cp);
        if (!tmp) { if (err) snprintf(err, errsz, "out of memory reading CGRA_PATH"); return -1; }
        char *save = NULL;
        int failed = 0;
        for (char *dir = strtok_r(tmp, ":", &save); dir; dir = strtok_r(NULL, ":", &save))
            if (load_tree(d, dir, err, errsz) != 0) { failed = 1; break; }
        free(tmp);
        if (failed) return -1;
    }

    /* project-local: $CGRA_CONFIG file, else ./.cgra/ */
    const char *proj = getenv("CGRA_CONFIG");
    if (proj && *proj) {
        if (dsl_parse_file(d, proj, err, errsz) != 0) return -1;
    } else if (load_tree(d, ".cgra", err, errsz) != 0) return -1;

    if (extra && *extra)
        return dsl_parse_file(d, extra, err, errsz);
    return 0;
}

/* -------------------------------------------------- built-in defaults */

const char *dsl_default_text(void)
{
    return
    "# Built-in CGRA configuration. Copy to ~/.config/cgra/ with `cgra init`\n"
    "# and extend or override any entity there.\n"
    "\n"
    "[device default]\n"
    "port    = /dev/ttyUSB1\n"
    "baud    = 115200\n"
    "timeout = 2000\n"
    "board   = basys3\n"
    "\n"
    "[device sim]\n"
    "doc     = in-process emulator, no FPGA needed\n"
    "port    = sim:\n"
    "\n"
    "# ---- element-wise vector modes (diagonal pattern, 4 lanes/transaction) ----\n"
    "[mode add]\n"
    "doc     = out = a + b\n"
    "pattern = diagonal\n"
    "op      = add\n"
    "a       = north\n"
    "b       = west\n"
    "\n"
    "[mode sub]\n"
    "doc     = out = a - b\n"
    "pattern = diagonal\n"
    "op      = sub\n"
    "\n"
    "[mode mul]\n"
    "doc     = out = a * b (low 16 bits)\n"
    "pattern = diagonal\n"
    "op      = mul\n"
    "\n"
    "[mode min]\n"
    "doc     = out = min(a, b)\n"
    "pattern = diagonal\n"
    "op      = min\n"
    "\n"
    "[mode max]\n"
    "doc     = out = max(a, b)\n"
    "pattern = diagonal\n"
    "op      = max\n"
    "\n"
    "[mode relu]\n"
    "doc     = out = max(a, 0)\n"
    "pattern = diagonal\n"
    "op      = max\n"
    "a       = north\n"
    "b       = const(0)\n"
    "\n"
    "[mode addi]\n"
    "doc     = out = a + imm (set with --imm)\n"
    "pattern = diagonal\n"
    "op      = add\n"
    "a       = north\n"
    "b       = const(0)\n"
    "\n"
    "[mode muli]\n"
    "doc     = out = a * imm (set with --imm)\n"
    "pattern = diagonal\n"
    "op      = mul\n"
    "a       = north\n"
    "b       = const(1)\n"
    "\n"
    "# ---- reduction mode (MAC accumulate on PE 0,0) ----\n"
    "[mode dot]\n"
    "doc     = scalar dot product of a and b\n"
    "pattern = reduce\n"
    "op      = mac\n"
    "a       = north\n"
    "b       = west\n"
    "\n"
    "# ---- systolic matrix-vector: y = A*x (drive with `cgra matvec`) ----\n"
    "[mode matvec]\n"
    "doc     = weight-stationary systolic y = A*x (use `cgra matvec`)\n"
    "pattern = systolic\n"
    "\n"
    "[mode conv]\n"
    "doc     = 1-D convolution via Toeplitz systolic (use `cgra conv`)\n"
    "pattern = conv\n"
    "\n"
    "[mode scan]\n"
    "doc     = inclusive prefix sum on row 0 (use `cgra scan`)\n"
    "pattern = scan\n"
    "\n"
    "# ---- custom per-PE example: out = (a + b) then squared on the diagonal ----\n"
    "[mode custom_example]\n"
    "doc     = template for hand-placed PEs; edit freely\n"
    "pattern = custom\n"
    "out     = diag\n"
    "steps   = rows\n"
    "pe 0,0  = op=add a=north b=west\n"
    "pe 1,1  = op=add a=north b=west\n"
    "pe 2,2  = op=add a=north b=west\n"
    "pe 3,3  = op=add a=north b=west\n"
    "pe 0,1  = op=pass a=north\n"
    "pe 0,2  = op=pass a=north\n"
    "pe 0,3  = op=pass a=north\n"
    "pe 1,2  = op=pass a=north\n"
    "pe 1,3  = op=pass a=north\n"
    "pe 2,3  = op=pass a=north\n"
    "pe 1,0  = op=pass a=west\n"
    "pe 2,0  = op=pass a=west\n"
    "pe 3,0  = op=pass a=west\n"
    "pe 2,1  = op=pass a=west\n"
    "pe 3,1  = op=pass a=west\n"
    "pe 3,2  = op=pass a=west\n"
    "\n"
    "# ---- multi-stage pipelines (host chains the stages) ----\n"
    "[pipeline saxpy]\n"
    "doc     = out = a * 3 + 7\n"
    "stage   = muli imm=3\n"
    "stage   = addi imm=7\n"
    "\n"
    "# ---- data binding profiles ----\n"
    "[io dec]\n"
    "format  = dec\n"
    "width   = 16\n"
    "sep     = ws\n"
    "\n"
    "[io hex]\n"
    "format  = hex\n"
    "width   = 16\n"
    "sep     = ws\n";
}
