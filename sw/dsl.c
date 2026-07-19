/*
 * dsl.c — parser and discovery for the .cgra configuration language.
 */

#define _POSIX_C_SOURCE 200809L

#include "dsl.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* -------------------------------------------------- small helpers */

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

static void copy_field(char *dst, size_t dstsz, const char *src)
{
    size_t n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
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

static void var_set(dsl_ctx *d, const char *name, const char *val)
{
    for (int i = 0; i < d->nvar; i++)
        if (strcmp(d->var[i].name, name) == 0) {
            copy_field(d->var[i].val, sizeof(d->var[i].val), val);
            return;
        }
    if (d->nvar >= DSL_MAX_VARS)
        return;
    copy_field(d->var[d->nvar].name, sizeof(d->var[d->nvar].name), name);
    copy_field(d->var[d->nvar].val, sizeof(d->var[d->nvar].val), val);
    d->nvar++;
}

/* Expand $NAME references (and ${NAME}) using the defined variables. Unknown
 * names are left verbatim. */
static void subst_vars(dsl_ctx *d, const char *in, char *out, size_t outsz)
{
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < outsz; ) {
        if (in[i] == '$') {
            int braced = (in[i + 1] == '{');
            size_t s = i + 1 + (size_t)braced;
            size_t e = s;
            while ((isalnum((unsigned char)in[e]) || in[e] == '_'))
                e++;
            char name[DSL_NAME];
            size_t len = e - s;
            if (len > 0 && len < sizeof(name)) {
                memcpy(name, in + s, len);
                name[len] = '\0';
                const char *v = var_lookup(d, name);
                if (v) {
                    for (size_t k = 0; v[k] && o + 1 < outsz; k++)
                        out[o++] = v[k];
                    i = e + (braced && in[e] == '}' ? 1 : 0);
                    continue;
                }
            }
        }
        out[o++] = in[i++];
    }
    out[o] = '\0';
}

/* -------------------------------------------------- directive handlers */

enum sect { SECT_NONE, SECT_DEVICE, SECT_MODE, SECT_PIPE, SECT_IO };

static dsl_pe *mode_pe_get(dsl_mode *m, int r, int c)
{
    for (int i = 0; i < m->npe; i++)
        if (m->pe[i].r == r && m->pe[i].c == c)
            return &m->pe[i];
    if (m->npe >= CGRA_NUM_PE)
        return NULL;
    dsl_pe *p = &m->pe[m->npe++];
    memset(p, 0, sizeof(*p));
    p->r = r;
    p->c = c;
    return p;
}

/* Fill a dsl_pe from "op=mac a=north b=west". */
static void parse_pe_value(dsl_pe *p, const char *value)
{
    char tmp[DSL_VAL];
    copy_field(tmp, sizeof(tmp), value);
    char *save = NULL;
    for (char *tok = strtok_r(tmp, " \t", &save); tok;
         tok = strtok_r(NULL, " \t", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *k = tok, *v = eq + 1;
        if (strcmp(k, "op") == 0)      copy_field(p->op, sizeof(p->op), v);
        else if (strcmp(k, "a") == 0)  copy_field(p->a, sizeof(p->a), v);
        else if (strcmp(k, "b") == 0)  copy_field(p->b, sizeof(p->b), v);
    }
}

static int apply_directive(dsl_ctx *d, enum sect sect, void *cur,
                           const char *key, const char *value,
                           const char *origin, int lineno,
                           char *err, size_t errsz)
{
    (void)d;
    /* 'doc' is accepted (and ignored) in sections without a doc field. */
    if (strcmp(key, "doc") == 0 && (sect == SECT_DEVICE || sect == SECT_IO))
        return 0;

    switch (sect) {
    case SECT_DEVICE: {
        dsl_device *e = cur;
        if (strcmp(key, "port") == 0)         copy_field(e->port, sizeof(e->port), value);
        else if (strcmp(key, "baud") == 0)    e->baud = atoi(value);
        else if (strcmp(key, "timeout") == 0) e->timeout_ms = atoi(value);
        else if (strcmp(key, "board") == 0)   copy_field(e->board, sizeof(e->board), value);
        else goto unknown;
        return 0;
    }
    case SECT_MODE: {
        dsl_mode *e = cur;
        if (strncmp(key, "pe ", 3) == 0 || strncmp(key, "pe\t", 3) == 0) {
            int r = -1, c = -1;
            if (sscanf(key + 3, " %d , %d", &r, &c) != 2 ||
                r < 0 || r >= CGRA_ROWS || c < 0 || c >= CGRA_COLS) {
                snprintf(err, errsz, "%s:%d: bad PE coordinate '%s'", origin, lineno, key);
                return -1;
            }
            dsl_pe *p = mode_pe_get(e, r, c);
            if (!p) {
                snprintf(err, errsz, "%s:%d: too many PEs", origin, lineno);
                return -1;
            }
            parse_pe_value(p, value);
            return 0;
        }
        if (strcmp(key, "doc") == 0)          copy_field(e->doc, sizeof(e->doc), value);
        else if (strcmp(key, "pattern") == 0) copy_field(e->pattern, sizeof(e->pattern), value);
        else if (strcmp(key, "op") == 0)      copy_field(e->op, sizeof(e->op), value);
        else if (strcmp(key, "a") == 0)       copy_field(e->a, sizeof(e->a), value);
        else if (strcmp(key, "b") == 0)       copy_field(e->b, sizeof(e->b), value);
        else if (strcmp(key, "steps") == 0)   copy_field(e->steps, sizeof(e->steps), value);
        else if (strcmp(key, "out") == 0)     copy_field(e->out, sizeof(e->out), value);
        else if (strcmp(key, "reset") == 0)   copy_field(e->reset, sizeof(e->reset), value);
        else goto unknown;
        return 0;
    }
    case SECT_PIPE: {
        dsl_pipeline *e = cur;
        if (strcmp(key, "doc") == 0) {
            copy_field(e->doc, sizeof(e->doc), value);
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
            copy_field(tmp, sizeof(tmp), value);
            char *save = NULL;
            char *first = strtok_r(tmp, " \t", &save);
            if (first)
                copy_field(st->mode, sizeof(st->mode), first);
            for (char *tok = strtok_r(NULL, " \t", &save); tok;
                 tok = strtok_r(NULL, " \t", &save)) {
                if (strncmp(tok, "imm=", 4) == 0) {
                    st->imm = strtol(tok + 4, NULL, 0);
                    st->has_imm = 1;
                }
            }
            return 0;
        }
        goto unknown;
    }
    case SECT_IO: {
        dsl_io *e = cur;
        if (strcmp(key, "format") == 0)     copy_field(e->format, sizeof(e->format), value);
        else if (strcmp(key, "width") == 0) e->width = atoi(value);
        else if (strcmp(key, "sep") == 0)   copy_field(e->sep, sizeof(e->sep), value);
        else goto unknown;
        return 0;
    }
    default:
        snprintf(err, errsz, "%s:%d: directive '%s' outside any section",
                 origin, lineno, key);
        return -1;
    }

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
    if (sub)
        snprintf(cand, sizeof(cand), "%s/%s/%s", root, sub, path);
    else
        snprintf(cand, sizeof(cand), "%s/%s", root, path);
    if (!file_exists(cand))
        return 0;
    copy_field(out, outsz, cand);
    return 1;
}

static int find_include(const char *origin, const char *path,
                        char *out, size_t outsz)
{
    if (path[0] == '/') {
        copy_field(out, outsz, path);
        return file_exists(out);
    }

    /* 1. relative to the including file */
    const char *slash = strrchr(origin, '/');
    if (slash) {
        char cand[2 * DSL_VAL];
        snprintf(cand, sizeof(cand), "%.*s/%s", (int)(slash - origin), origin, path);
        if (file_exists(cand)) { copy_field(out, outsz, cand); return 1; }
    }

    /* 2. CGRA_PATH entries */
    const char *cp = getenv("CGRA_PATH");
    if (cp && *cp) {
        char tmp[DSL_VAL];
        snprintf(tmp, sizeof(tmp), "%s", cp);
        char *save = NULL;
        for (char *dir = strtok_r(tmp, ":", &save); dir; dir = strtok_r(NULL, ":", &save))
            if (try_root(dir, NULL, path, out, outsz))
                return 1;
    }

    /* 3. user config dir and its stdlib/ */
    char udir[DSL_VAL];
    if (dsl_user_dir(udir, sizeof(udir)) == 0) {
        if (try_root(udir, NULL, path, out, outsz)) return 1;
        if (try_root(udir, "stdlib", path, out, outsz)) return 1;
    }

    /* 4. system locations */
    if (try_root("/usr/share/cgra/stdlib", NULL, path, out, outsz)) return 1;
    if (try_root("/etc/cgra", NULL, path, out, outsz)) return 1;

    return 0;
}

int dsl_parse(dsl_ctx *d, const char *text, const char *origin,
              char *err, size_t errsz)
{
    char errbuf[DSL_VAL];
    if (!err) { err = errbuf; errsz = sizeof(errbuf); }
    err[0] = '\0';

    enum sect sect = SECT_NONE;
    void *cur = NULL;

    char *copy = strdup(text);
    if (!copy) {
        snprintf(err, errsz, "%s: out of memory", origin);
        return -1;
    }

    int lineno = 0, rc = 0;
    char *save_line = NULL;
    for (char *line = strtok_r(copy, "\n", &save_line); line;
         line = strtok_r(NULL, "\n", &save_line)) {
        lineno++;
        strip_comment(line);
        char *s = trim(line);
        if (*s == '\0')
            continue;

        /* include DIRECTIVE: pull in another .cgra file, searched across the
         * include roots (relative, CGRA_PATH, stdlib, /etc). */
        if (strncmp(s, "include", 7) == 0 && (s[7] == ' ' || s[7] == '\t')) {
            char *path = trim(s + 7);
            if ((path[0] == '"' || path[0] == '\'') && strlen(path) >= 2) {
                path[strlen(path) - 1] = '\0';
                path++;
            }
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
            size_t k = 0;
            while (rest[k] && (isalnum((unsigned char)rest[k]) || rest[k] == '_')
                   && k < sizeof(name) - 1) {
                name[k] = rest[k];
                k++;
            }
            char *v = trim(rest + k);
            if (*v == '=')
                v = trim(v + 1);
            char expanded[DSL_VAL];
            subst_vars(d, v, expanded, sizeof(expanded));
            var_set(d, name, expanded);
            continue;
        }

        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close) {
                snprintf(err, errsz, "%s:%d: missing ']'", origin, lineno);
                rc = -1;
                break;
            }
            *close = '\0';
            char *hdr = trim(s + 1);
            char type[DSL_TOK] = {0}, name[DSL_NAME] = {0};
            if (sscanf(hdr, "%31s %47s", type, name) != 2) {
                snprintf(err, errsz, "%s:%d: section needs 'type name'", origin, lineno);
                rc = -1;
                break;
            }
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
        subst_vars(d, value, expanded, sizeof(expanded));
        if (apply_directive(d, sect, cur, key, expanded, origin, lineno, err, errsz) != 0) {
            rc = -1;
            break;
        }
    }

    free(copy);
    return rc;
}

int dsl_parse_file(dsl_ctx *d, const char *path, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) snprintf(err, errsz, "cannot open %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);

    int rc = dsl_parse(d, buf, path, err, errsz);
    free(buf);
    if (rc == 0 && d->npath < DSL_MAX_PATHS)
        copy_field(d->paths[d->npath++], DSL_VAL, path);
    return rc;
}

/* -------------------------------------------------- discovery */

int dsl_user_dir(char *buf, size_t bufsz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(buf, bufsz, "%s/cgra", xdg);
        return 0;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(buf, bufsz, "%s/.config/cgra", home);
        return 0;
    }
    return -1;
}

static int ends_with_cgra(const char *name)
{
    size_t n = strlen(name);
    return n > 5 && strcmp(name + n - 5, ".cgra") == 0;
}

/* Parse config.cgra first (if present), then the remaining *.cgra sorted. */
static void load_dir(dsl_ctx *d, const char *dir)
{
    struct stat stbuf;
    if (stat(dir, &stbuf) != 0 || !S_ISDIR(stbuf.st_mode))
        return;

    char path[DSL_VAL + 260];
    snprintf(path, sizeof(path), "%s/config.cgra", dir);
    if (stat(path, &stbuf) == 0)
        dsl_parse_file(d, path, NULL, 0);

    struct dirent **names = NULL;
    int n = scandir(dir, &names, NULL, alphasort);
    if (n < 0)
        return;
    for (int i = 0; i < n; i++) {
        if (ends_with_cgra(names[i]->d_name) &&
            strcmp(names[i]->d_name, "config.cgra") != 0) {
            snprintf(path, sizeof(path), "%s/%s", dir, names[i]->d_name);
            dsl_parse_file(d, path, NULL, 0);
        }
        free(names[i]);
    }
    free(names);
}

int dsl_load_defaults(dsl_ctx *d, char *err, size_t errsz)
{
    return dsl_parse(d, dsl_default_text(), "<builtin>", err, errsz);
}

/* Load a directory's top-level *.cgra, then its conf.d/ drop-ins (alphabetical,
 * so 99_*.cgra overrides 01_*.cgra by name). */
static void load_tree(dsl_ctx *d, const char *dir)
{
    char confd[DSL_VAL + 16];
    load_dir(d, dir);
    snprintf(confd, sizeof(confd), "%s/conf.d", dir);
    load_dir(d, confd);
}

int dsl_load(dsl_ctx *d, const char *extra, char *err, size_t errsz)
{
    if (dsl_load_defaults(d, err, errsz) != 0)
        return -1;

    load_tree(d, "/etc/cgra");

    char udir[DSL_VAL];
    if (dsl_user_dir(udir, sizeof(udir)) == 0)
        load_tree(d, udir);

    /* CGRA_PATH: extra config roots (like PATH), colon-separated. */
    const char *cp = getenv("CGRA_PATH");
    if (cp && *cp) {
        char tmp[DSL_VAL];
        snprintf(tmp, sizeof(tmp), "%s", cp);
        char *save = NULL;
        for (char *dir = strtok_r(tmp, ":", &save); dir; dir = strtok_r(NULL, ":", &save))
            load_tree(d, dir);
    }

    /* project-local: $CGRA_CONFIG file, else ./.cgra/ */
    const char *proj = getenv("CGRA_CONFIG");
    if (proj && *proj)
        dsl_parse_file(d, proj, err, errsz);
    else
        load_tree(d, ".cgra");

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
