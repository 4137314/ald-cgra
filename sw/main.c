/*
 * main.c — the `cgra` command-line front end.
 *
 * High-level management of the CGRA accelerator on top of libcgra: it reads
 * the .cgra configuration language, resolves device profiles, compiles named
 * modes/pipelines and moves vectors in and out in UNIX-pipe style.
 *
 *   cgra modes | devices | pipelines | io | config
 *   cgra init [--force]
 *   cgra ping | reset | dump         [-d DEVICE]
 *   cgra show MODE [--imm N]
 *   cgra run  MODE [nums...] [-d DEVICE] [--a SRC] [--b SRC] [--imm N] [--steps N] [--io P]
 *   cgra pipe NAME [nums...] [-d DEVICE] [--a SRC] [--b SRC] [--io P]
 *   cgra selftest                    (runs against the sim: emulator)
 *
 * Input sources (--a/--b): inline "1 2 3", "@file", or "-" for stdin.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "cgra.h"
#include "dsl.h"
#include "compile.h"

#define MAX_VEC 4096

/* output / formatting state, set from the global options in main() */
static FILE       *g_out = NULL;         /* NULL means stdout */
static int         g_unsigned = 0;       /* interpret results as unsigned */
static const char *g_fmt_override = NULL;/* e.g. "json" from --json */

/* -------------------------------------------------- vector I/O */

static int parse_ints(const char *text, int16_t *out, size_t cap, size_t *n)
{
    *n = 0;
    const char *p = text;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ','))
            p++;
        if (*p == '\0')
            break;
        char *end = NULL;
        long v = strtol(p, &end, 0);
        if (end == p)
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
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
    }
    buf[len] = '\0';
    return buf;
}

/* Read a vector from an inline list, "@file", or "-" (stdin). */
static int read_vec(const char *src, int16_t *out, size_t cap, size_t *n)
{
    if (strcmp(src, "-") == 0) {
        char *buf = slurp(stdin);
        if (!buf) return -1;
        int rc = parse_ints(buf, out, cap, n);
        free(buf);
        return rc;
    }
    if (src[0] == '@') {
        FILE *f = fopen(src + 1, "rb");
        if (!f) { fprintf(stderr, "cgra: cannot open %s: %s\n", src + 1, strerror(errno)); return -1; }
        char *buf = slurp(f);
        fclose(f);
        if (!buf) return -1;
        int rc = parse_ints(buf, out, cap, n);
        free(buf);
        return rc;
    }
    return parse_ints(src, out, cap, n);
}

static void print_vec(const int16_t *v, size_t n, const dsl_io *io)
{
    FILE *out = g_out ? g_out : stdout;
    const char *fmt = g_fmt_override ? g_fmt_override
                    : (io && io->format[0] ? io->format : "dec");
    const char *sep = io && io->sep[0] ? io->sep : "ws";
    const char *between = strcmp(sep, "comma") == 0 ? ","
                        : strcmp(sep, "newline") == 0 ? "\n" : " ";

    if (strcmp(fmt, "json") == 0) {
        /* An object (not a bare array) so results compose with jq '.result'. */
        fprintf(out, "{\"count\": %zu, \"result\": [", n);
        for (size_t i = 0; i < n; i++) {
            if (i) fputs(", ", out);
            if (g_unsigned) fprintf(out, "%u", (unsigned)(uint16_t)v[i]);
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
        } else if (g_unsigned)
            fprintf(out, "%u", (unsigned)(uint16_t)v[i]);
        else
            fprintf(out, "%d", v[i]);
    }
    fputc('\n', out);
}

/* -------------------------------------------------- device resolution */

static int looks_literal(const char *s)
{
    return strchr(s, '/') != NULL || strncmp(s, "sim", 3) == 0;
}

/* Path of the file where `cgra probe` records the discovered device. */
static int device_state_path(char *buf, size_t sz)
{
    char udir[DSL_VAL];
    if (dsl_user_dir(udir, sizeof(udir)) != 0)
        return -1;
    snprintf(buf, sz, "%s/.device", udir);
    return 0;
}

static int read_device_state(char *buf, size_t sz)
{
    char p[DSL_VAL + 16];
    if (device_state_path(p, sizeof(p)) != 0)
        return -1;
    FILE *f = fopen(p, "r");
    if (!f)
        return -1;
    int ok = fgets(buf, (int)sz, f) != NULL;
    fclose(f);
    if (!ok)
        return -1;
    buf[strcspn(buf, "\r\n")] = '\0';
    return buf[0] ? 0 : -1;
}

static cgra_t *open_device(dsl_ctx *d, const char *name_opt, char *err, size_t errsz)
{
    const char *name = name_opt;
    if (!name) name = getenv("CGRA_DEVICE");
    if (!name || !*name) name = "default";

    /* "auto" resolves to the port saved by `cgra probe`. */
    char autobuf[DSL_VAL];
    if (strcmp(name, "auto") == 0) {
        if (read_device_state(autobuf, sizeof(autobuf)) != 0) {
            snprintf(err, errsz, "no saved device; run `cgra probe` first");
            return NULL;
        }
        name = autobuf;
    }

    char port[DSL_VAL] = {0};
    int baud = 115200, timeout = 2000;

    if (looks_literal(name)) {
        snprintf(port, sizeof(port), "%s", name);
    } else {
        dsl_device *dev = dsl_find_device(d, name);
        if (!dev) { snprintf(err, errsz, "unknown device profile '%.80s'", name); return NULL; }
        snprintf(port, sizeof(port), "%s", dev->port[0] ? dev->port : "/dev/ttyUSB1");
        if (dev->baud) baud = dev->baud;
        if (dev->timeout_ms) timeout = dev->timeout_ms;
    }

    const char *env_port = getenv("CGRA_PORT");
    if (env_port && *env_port) snprintf(port, sizeof(port), "%s", env_port);
    const char *env_baud = getenv("CGRA_BAUD");
    if (env_baud && *env_baud) baud = atoi(env_baud);

    cgra_t *dev = cgra_open(port, (unsigned)baud);
    if (!dev) { snprintf(err, errsz, "cannot open %s at %d baud", port, baud); return NULL; }
    cgra_set_timeout(dev, (unsigned)timeout);
    return dev;
}

/* -------------------------------------------------- listing commands */

static int cmd_devices(dsl_ctx *d)
{
    for (int i = 0; i < d->ndev; i++)
        printf("%-12s %s\n", d->dev[i].name, d->dev[i].port);
    return 0;
}

static int cmd_modes(dsl_ctx *d)
{
    for (int i = 0; i < d->nmode; i++) {
        const dsl_mode *m = &d->mode[i];
        printf("%-16s %-9s %s\n", m->name,
               m->pattern[0] ? m->pattern : "diagonal", m->doc);
    }
    return 0;
}

static int cmd_pipelines(dsl_ctx *d)
{
    for (int i = 0; i < d->npipe; i++) {
        const dsl_pipeline *p = &d->pipe[i];
        printf("%-16s %s\n", p->name, p->doc);
        for (int s = 0; s < p->nstage; s++) {
            if (p->stage[s].has_imm)
                printf("    -> %s imm=%ld\n", p->stage[s].mode, p->stage[s].imm);
            else
                printf("    -> %s\n", p->stage[s].mode);
        }
    }
    return 0;
}

static int cmd_io(dsl_ctx *d)
{
    for (int i = 0; i < d->nio; i++)
        printf("%-10s format=%s width=%d sep=%s\n", d->io[i].name,
               d->io[i].format, d->io[i].width, d->io[i].sep);
    return 0;
}

static int cmd_config(dsl_ctx *d)
{
    char udir[DSL_VAL];
    printf("user config dir : %s\n", dsl_user_dir(udir, sizeof(udir)) == 0 ? udir : "(HOME unset)");
    printf("loaded files    : %s\n", d->npath ? "" : "(built-in defaults only)");
    for (int i = 0; i < d->npath; i++)
        printf("                  %s\n", d->paths[i]);
    printf("devices=%d modes=%d pipelines=%d io=%d\n",
           d->ndev, d->nmode, d->npipe, d->nio);
    return 0;
}

static int cmd_check(dsl_ctx *d)
{
    int errors = 0;
    char err[DSL_VAL];
    uint32_t cfg[CGRA_NUM_PE];

    for (int i = 0; i < d->nmode; i++) {
        if (mode_compile(&d->mode[i], 0, 0, cfg, err, sizeof(err)) != 0) {
            printf("error: mode %s: %s\n", d->mode[i].name, err);
            errors++;
        }
    }
    for (int i = 0; i < d->npipe; i++) {
        dsl_pipeline *p = &d->pipe[i];
        if (p->nstage == 0) {
            printf("warning: pipeline %s has no stages\n", p->name);
        }
        for (int s = 0; s < p->nstage; s++) {
            if (!dsl_find_mode(d, p->stage[s].mode)) {
                printf("error: pipeline %s: stage '%s' is not a defined mode\n",
                       p->name, p->stage[s].mode);
                errors++;
            }
        }
    }
    if (errors == 0)
        printf("ok: %d modes, %d pipelines, %d devices, %d io profiles\n",
               d->nmode, d->npipe, d->ndev, d->nio);
    else
        printf("%d error(s)\n", errors);
    return errors ? 1 : 0;
}

/* -------------------------------------------------- init */

static int mkdir_p(const char *path)
{
    char tmp[DSL_VAL];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), in)) > 0)
        fwrite(b, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

/* Copy the shipped stdlib .cgra files into <udir>/stdlib so that
 * `include "linalg.cgra"` works out of the box. Source is the first of
 * $CGRA_STDLIB, the installed share dir, or the in-tree sw/config/stdlib. */
static void init_stdlib(const char *udir)
{
    const char *cands[3];
    int nc = 0;
    const char *env = getenv("CGRA_STDLIB");
    if (env && *env) cands[nc++] = env;
    cands[nc++] = "/usr/share/cgra/stdlib";
    cands[nc++] = "sw/config/stdlib";

    for (int c = 0; c < nc; c++) {
        char pat[DSL_VAL + 16];
        snprintf(pat, sizeof(pat), "%s/*.cgra", cands[c]);
        glob_t g;
        if (glob(pat, 0, NULL, &g) != 0 || g.gl_pathc == 0) {
            globfree(&g);
            continue;
        }
        char sdir[DSL_VAL + 16];
        snprintf(sdir, sizeof(sdir), "%s/stdlib", udir);
        mkdir_p(sdir);
        int copied = 0;
        for (size_t i = 0; i < g.gl_pathc; i++) {
            const char *base = strrchr(g.gl_pathv[i], '/');
            base = base ? base + 1 : g.gl_pathv[i];
            char dst[2 * DSL_VAL];
            snprintf(dst, sizeof(dst), "%s/%s", sdir, base);
            if (copy_file(g.gl_pathv[i], dst) == 0)
                copied++;
        }
        globfree(&g);
        printf("installed %d stdlib file(s) to %s\n", copied, sdir);
        return;
    }
    printf("note: stdlib not found; set CGRA_PATH to your .cgra library dir\n");
}

static int cmd_init(int force)
{
    char udir[DSL_VAL];
    if (dsl_user_dir(udir, sizeof(udir)) != 0) {
        fprintf(stderr, "cgra: HOME/XDG_CONFIG_HOME not set\n");
        return 1;
    }
    if (mkdir_p(udir) != 0) {
        fprintf(stderr, "cgra: cannot create %s: %s\n", udir, strerror(errno));
        return 1;
    }
    char path[DSL_VAL + 16];
    snprintf(path, sizeof(path), "%s/config.cgra", udir);

    struct stat st;
    if (stat(path, &st) == 0 && !force) {
        fprintf(stderr, "cgra: %s already exists (use --force to overwrite)\n", path);
        return 1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cgra: cannot write %s: %s\n", path, strerror(errno)); return 1; }
    fputs(dsl_default_text(), f);
    fclose(f);
    printf("wrote %s\n", path);
    init_stdlib(udir);
    return 0;
}

/* -------------------------------------------------- device commands */

static int cmd_ping(dsl_ctx *d, const char *devname)
{
    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cgra_info_t info;
    int rc = cgra_identify(dev, &info);
    if (rc == CGRA_OK) {
        printf("cgra ok: protocol v%u, %ux%u array, %u-bit datapath\n",
               info.version, info.rows, info.cols, info.data_w);
        if (info.rows != CGRA_ROWS || info.cols != CGRA_COLS || info.data_w != CGRA_DATA_W)
            printf("warning: device geometry differs from this build "
                   "(%dx%d, %d-bit)\n", CGRA_ROWS, CGRA_COLS, CGRA_DATA_W);
    } else {
        fprintf(stderr, "cgra: ping failed: %s\n", cgra_strerror(rc));
    }
    cgra_close(dev);
    return rc == CGRA_OK ? 0 : 1;
}

static int cmd_reset(dsl_ctx *d, const char *devname)
{
    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int rc = cgra_reset_datapath(dev);
    if (rc != CGRA_OK) fprintf(stderr, "cgra: reset failed: %s\n", cgra_strerror(rc));
    cgra_close(dev);
    return rc == CGRA_OK ? 0 : 1;
}

static int cmd_dump(dsl_ctx *d, const char *devname, const dsl_io *io)
{
    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int16_t regs[CGRA_NUM_PE];
    int rc = cgra_read_regs(dev, regs);
    cgra_close(dev);
    if (rc != CGRA_OK) { fprintf(stderr, "cgra: read failed: %s\n", cgra_strerror(rc)); return 1; }
    for (int r = 0; r < CGRA_ROWS; r++) {
        print_vec(&regs[r * CGRA_COLS], CGRA_COLS, io);
    }
    return 0;
}

/* -------------------------------------------------- show / run / pipe */

static int cmd_show(dsl_ctx *d, const char *modename, long imm, int has_imm, int verbose)
{
    dsl_mode *m = dsl_find_mode(d, modename);
    if (!m) { fprintf(stderr, "cgra: unknown mode '%s'\n", modename); return 1; }
    uint32_t cfg[CGRA_NUM_PE];
    char err[DSL_VAL];
    if (mode_compile(m, imm, has_imm, cfg, err, sizeof(err)) != 0) {
        fprintf(stderr, "cgra: %s\n", err);
        return 1;
    }
    printf("mode %s (pattern %s):\n", m->name, m->pattern[0] ? m->pattern : "diagonal");
    if (verbose) {
        for (int r = 0; r < CGRA_ROWS; r++)
            for (int c = 0; c < CGRA_COLS; c++) {
                char dec[64];
                cfg_decode(cfg[r * CGRA_COLS + c], dec, sizeof(dec));
                printf("  PE(%d,%d) %08X  %s\n", r, c, cfg[r * CGRA_COLS + c], dec);
            }
    } else {
        for (int r = 0; r < CGRA_ROWS; r++) {
            for (int c = 0; c < CGRA_COLS; c++)
                printf("  %08X", cfg[r * CGRA_COLS + c]);
            putchar('\n');
        }
    }
    return 0;
}

static int mode_needs_b(const dsl_mode *m)
{
    /* The b[] vector is injected on the west edge, so it is only required when
     * operand b actually reads from the west. */
    const char *b = m->b[0] ? m->b : "west";
    if (strcmp(m->pattern, "custom") == 0)
        return 0;   /* user's responsibility */
    return strcmp(b, "west") == 0;
}

/* Collect a[] and b[] from --a/--b, positionals, or stdin. */
static int gather_inputs(const char *opt_a, const char *opt_b,
                         char **pos, int npos,
                         int16_t *a, size_t *na, int16_t *b, size_t *nb,
                         int need_b)
{
    *na = 0; *nb = 0;
    if (opt_a) {
        if (read_vec(opt_a, a, MAX_VEC, na) != 0) { fprintf(stderr, "cgra: bad --a vector\n"); return -1; }
    } else if (npos > 0) {
        for (int i = 0; i < npos; i++) {
            size_t k;
            if (parse_ints(pos[i], a + *na, MAX_VEC - *na, &k) != 0) { fprintf(stderr, "cgra: bad input '%s'\n", pos[i]); return -1; }
            *na += k;
        }
    } else {
        if (read_vec("-", a, MAX_VEC, na) != 0) { fprintf(stderr, "cgra: bad stdin vector\n"); return -1; }
    }
    if (opt_b) {
        if (read_vec(opt_b, b, MAX_VEC, nb) != 0) { fprintf(stderr, "cgra: bad --b vector\n"); return -1; }
    }
    if (need_b) {
        if (*nb == 0) { fprintf(stderr, "cgra: this mode needs a second vector (--b)\n"); return -1; }
        if (*nb != *na) { fprintf(stderr, "cgra: --a and --b lengths differ (%zu vs %zu)\n", *na, *nb); return -1; }
    }
    return 0;
}

static int cmd_run(dsl_ctx *d, const char *devname, const char *modename,
                   char **pos, int npos, const char *opt_a, const char *opt_b,
                   long imm, int has_imm, int steps, int has_steps,
                   const dsl_io *io)
{
    dsl_mode *m = dsl_find_mode(d, modename);
    if (!m) { fprintf(stderr, "cgra: unknown mode '%s'\n", modename); return 1; }

    dsl_mode local = *m;
    if (has_steps) snprintf(local.steps, sizeof(local.steps), "%d", steps);

    static int16_t a[MAX_VEC], b[MAX_VEC], out[MAX_VEC];
    size_t na, nb;
    if (gather_inputs(opt_a, opt_b, pos, npos, a, &na, b, &nb, mode_needs_b(m)) != 0)
        return 1;

    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }

    int nout = mode_run(dev, &local, a, nb ? b : NULL, na, imm, has_imm,
                        out, MAX_VEC, err, sizeof(err));
    cgra_close(dev);
    if (nout < 0) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    print_vec(out, (size_t)nout, io);
    return 0;
}

static int cmd_pipe(dsl_ctx *d, const char *devname, const char *pipename,
                    char **pos, int npos, const char *opt_a, const char *opt_b,
                    const dsl_io *io)
{
    dsl_pipeline *pl = dsl_find_pipeline(d, pipename);
    if (!pl) { fprintf(stderr, "cgra: unknown pipeline '%s'\n", pipename); return 1; }
    if (pl->nstage == 0) { fprintf(stderr, "cgra: pipeline '%s' has no stages\n", pipename); return 1; }

    /* First stage may consume b; later stages are unary and chain a<-out. */
    dsl_mode *m0 = dsl_find_mode(d, pl->stage[0].mode);
    if (!m0) { fprintf(stderr, "cgra: pipeline stage mode '%s' not found\n", pl->stage[0].mode); return 1; }

    static int16_t a[MAX_VEC], b[MAX_VEC], out[MAX_VEC];
    size_t na, nb;
    if (gather_inputs(opt_a, opt_b, pos, npos, a, &na, b, &nb, mode_needs_b(m0)) != 0)
        return 1;

    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }

    for (int s = 0; s < pl->nstage; s++) {
        dsl_mode *m = dsl_find_mode(d, pl->stage[s].mode);
        if (!m) { fprintf(stderr, "cgra: pipeline stage mode '%s' not found\n", pl->stage[s].mode); cgra_close(dev); return 1; }
        const int16_t *bin = (s == 0 && nb) ? b : NULL;
        int nout = mode_run(dev, m, a, bin, na,
                            pl->stage[s].imm, pl->stage[s].has_imm,
                            out, MAX_VEC, err, sizeof(err));
        if (nout < 0) { fprintf(stderr, "cgra: stage %s: %s\n", m->name, err); cgra_close(dev); return 1; }
        memcpy(a, out, (size_t)nout * sizeof(int16_t));
        na = (size_t)nout;
    }
    cgra_close(dev);
    print_vec(a, na, io);
    return 0;
}

static int cmd_matvec(dsl_ctx *d, const char *devname, const char *m_src,
                      const char *x_src, int cols, const dsl_io *io)
{
    if (!m_src) { fprintf(stderr, "cgra: matvec needs a matrix (-m/--matrix)\n"); return 1; }
    if (!x_src) { fprintf(stderr, "cgra: matvec needs a vector (--x)\n"); return 1; }

    static int16_t A[MAX_VEC], x[MAX_VEC], y[MAX_VEC];
    size_t nA, nx;
    if (read_vec(m_src, A, MAX_VEC, &nA) != 0) { fprintf(stderr, "cgra: bad matrix\n"); return 1; }
    if (read_vec(x_src, x, MAX_VEC, &nx) != 0) { fprintf(stderr, "cgra: bad vector\n"); return 1; }

    int N = cols > 0 ? cols : (int)nx;
    if (N <= 0) { fprintf(stderr, "cgra: cannot determine matrix width\n"); return 1; }
    if ((int)nx != N) { fprintf(stderr, "cgra: x length %zu != matrix width %d\n", nx, N); return 1; }
    if (nA % (size_t)N != 0) { fprintf(stderr, "cgra: matrix size %zu not a multiple of width %d\n", nA, N); return 1; }
    int M = (int)(nA / (size_t)N);

    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int rc = matvec_run(dev, A, M, N, x, y, err, sizeof(err));
    cgra_close(dev);
    if (rc != CGRA_OK) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    print_vec(y, (size_t)M, io);
    return 0;
}

static int scan_op_code(const char *name)
{
    if (!name || !strcmp(name, "sum") || !strcmp(name, "add")) return CGRA_OP_ADD;
    if (!strcmp(name, "max")) return CGRA_OP_MAX;
    if (!strcmp(name, "min")) return CGRA_OP_MIN;
    if (!strcmp(name, "prod") || !strcmp(name, "mul")) return CGRA_OP_MUL;
    return -1;
}

static int cmd_scan(dsl_ctx *d, const char *devname, char **pos, int npos,
                    const char *opt_a, const char *opt_op, const dsl_io *io)
{
    int op = scan_op_code(opt_op);
    if (op < 0) { fprintf(stderr, "cgra: --op must be sum|max|min|prod\n"); return 1; }

    static int16_t a[MAX_VEC], b[MAX_VEC], out[MAX_VEC];
    size_t na, nb;
    if (gather_inputs(opt_a, NULL, pos, npos, a, &na, b, &nb, 0) != 0)
        return 1;

    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int n = scan_run(dev, a, (int)na, op, out, err, sizeof(err));
    cgra_close(dev);
    if (n < 0) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    print_vec(out, (size_t)n, io);
    return 0;
}

static int cmd_conv(dsl_ctx *d, const char *devname, const char *h_src,
                    const char *x_src, const char *opt_a, const char *opt_b,
                    const dsl_io *io)
{
    const char *hs = h_src ? h_src : opt_a;
    const char *xs = x_src ? x_src : opt_b;
    if (!hs) { fprintf(stderr, "cgra: conv needs kernel coefficients (-m/--matrix or --a)\n"); return 1; }
    if (!xs) { fprintf(stderr, "cgra: conv needs a signal (--x or --b)\n"); return 1; }

    static int16_t h[MAX_VEC], x[MAX_VEC], out[MAX_VEC];
    size_t nh, nx;
    if (read_vec(hs, h, MAX_VEC, &nh) != 0 || nh == 0) { fprintf(stderr, "cgra: bad kernel\n"); return 1; }
    if (read_vec(xs, x, MAX_VEC, &nx) != 0 || nx == 0) { fprintf(stderr, "cgra: bad signal\n"); return 1; }

    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int n = conv_run(dev, h, (int)nh, x, (int)nx, out, MAX_VEC, err, sizeof(err));
    cgra_close(dev);
    if (n < 0) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    print_vec(out, (size_t)n, io);
    return 0;
}

static int cmd_bench(dsl_ctx *d, const char *devname, const char *modename,
                     int size, int repeat)
{
    const char *name = modename ? modename : "add";
    dsl_mode *m = dsl_find_mode(d, name);
    if (!m) { fprintf(stderr, "cgra: unknown mode '%s'\n", name); return 1; }
    if (size <= 0) size = 256;
    if (size > MAX_VEC) size = MAX_VEC;
    if (repeat <= 0) repeat = 100;

    static int16_t a[MAX_VEC], b[MAX_VEC], out[MAX_VEC];
    for (int i = 0; i < size; i++) { a[i] = (int16_t)(i * 3 + 1); b[i] = (int16_t)(i - 7); }

    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }

    cgra_reset_stats(dev);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int r = 0; r < repeat; r++) {
        int rc = mode_run(dev, m, a, b, (size_t)size, 0, 0, out, MAX_VEC, err, sizeof(err));
        if (rc < 0) { fprintf(stderr, "cgra: %s\n", err); cgra_close(dev); return 1; }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    cgra_stats_t st;
    cgra_get_stats(dev, &st);
    cgra_close(dev);

    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    double per = ms / repeat;
    double eps = (double)size * repeat / (ms / 1e3);
    printf("bench %s: %d elems x %d iters in %.2f ms (%.3f ms/iter, %.0f elems/s)\n",
           name, size, repeat, ms, per, eps);
    printf("  link: %lu transactions, %lu retries, %lu tx bytes, %lu rx bytes\n",
           st.transactions, st.retries, st.tx_bytes, st.rx_bytes);
    return 0;
}

static void emulate_ready(const char *path, void *user)
{
    (void)user;
    printf("cgra emulator serving on %s\n", path);
    printf("point tools at it, e.g.:  cgra -d %s ping   (Ctrl-C to stop)\n", path);
    fflush(stdout);
}

static int cmd_emulate(void)
{
    int rc = cgra_emulate_pty(emulate_ready, NULL);
    if (rc != CGRA_OK) {
        fprintf(stderr, "cgra: emulator error: %s\n", cgra_strerror(rc));
        return 1;
    }
    return 0;
}

/* -------------------------------------------------- known-answer checks */

/* Runs every kernel/pattern on an already-open device and checks it against a
 * host reference. Prints one line per check; returns the number of failures.
 * Shared by `selftest` (emulator) and `probe` (real device bring-up). */
static int device_checks(cgra_t *dev, dsl_ctx *d)
{
    char err[DSL_VAL];
    int16_t a[16], b[16], out[16];
    for (int i = 0; i < 16; i++) { a[i] = (int16_t)(i - 5); b[i] = (int16_t)(2 * i + 1); }

    int fails = 0;
    struct { const char *mode; long imm; int has_imm; } cases[] = {
        {"add", 0, 0}, {"sub", 0, 0}, {"mul", 0, 0},
        {"min", 0, 0}, {"max", 0, 0}, {"relu", 0, 0},
        {"addi", 100, 1}, {"muli", 3, 1},
    };
    for (size_t t = 0; t < sizeof(cases) / sizeof(cases[0]); t++) {
        dsl_mode *m = dsl_find_mode(d, cases[t].mode);
        int n = mode_run(dev, m, a, b, 16, cases[t].imm, cases[t].has_imm, out, 16, err, sizeof(err));
        if (n < 0) { printf("FAIL  %-6s %s\n", cases[t].mode, err); fails++; continue; }
        int ok = 1;
        for (int i = 0; i < 16; i++) {
            int16_t e;
            const char *mo = cases[t].mode;
            if (!strcmp(mo, "add")) e = (int16_t)(a[i] + b[i]);
            else if (!strcmp(mo, "sub")) e = (int16_t)(a[i] - b[i]);
            else if (!strcmp(mo, "mul")) e = (int16_t)(a[i] * b[i]);
            else if (!strcmp(mo, "min")) e = a[i] < b[i] ? a[i] : b[i];
            else if (!strcmp(mo, "max")) e = a[i] > b[i] ? a[i] : b[i];
            else if (!strcmp(mo, "relu")) e = a[i] > 0 ? a[i] : 0;
            else if (!strcmp(mo, "addi")) e = (int16_t)(a[i] + 100);
            else e = (int16_t)(a[i] * 3);
            if (out[i] != e) { ok = 0; break; }
        }
        printf("%-5s %-6s\n", ok ? "ok" : "FAIL", cases[t].mode);
        if (!ok) fails++;
    }

    /* reduction */
    dsl_mode *dotm = dsl_find_mode(d, "dot");
    int n = mode_run(dev, dotm, a, b, 16, 0, 0, out, 16, err, sizeof(err));
    int16_t acc = 0;
    for (int i = 0; i < 16; i++) acc = (int16_t)(acc + (int16_t)(a[i] * b[i]));
    if (n == 1 && out[0] == acc) printf("ok    dot = %d\n", out[0]);
    else { printf("FAIL  dot (%d vs %d)\n", n == 1 ? out[0] : -1, acc); fails++; }

    /* systolic matrix-vector, non-square (6x5) to exercise tiling */
    {
        enum { MM = 6, NN = 5 };
        int16_t A[MM * NN], xv[NN], yv[MM], yref[MM];
        for (int i = 0; i < MM; i++) {
            for (int j = 0; j < NN; j++) A[i * NN + j] = (int16_t)(i - j + 1);
        }
        for (int j = 0; j < NN; j++) xv[j] = (int16_t)(j + 1);
        for (int i = 0; i < MM; i++) {
            int32_t s = 0;
            for (int j = 0; j < NN; j++) s += A[i * NN + j] * xv[j];
            yref[i] = (int16_t)s;
        }
        int mrc = matvec_run(dev, A, MM, NN, xv, yv, err, sizeof(err));
        int ok = (mrc == CGRA_OK) && memcmp(yv, yref, sizeof yref) == 0;
        printf("%-5s matvec 6x5\n", ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    /* prefix scan across two blocks (9 elements) */
    {
        int16_t sa[9], so[9], sref[9];
        int32_t run = 0;
        for (int i = 0; i < 9; i++) { sa[i] = (int16_t)(i - 3); run += sa[i]; sref[i] = (int16_t)run; }
        int srn = scan_run(dev, sa, 9, CGRA_OP_ADD, so, err, sizeof(err));
        int ok = (srn == 9) && memcmp(so, sref, sizeof sref) == 0;
        printf("%-5s scan(9)\n", ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    /* full 1-D convolution */
    {
        int16_t hh[3] = {1, 2, 3}, xx[5] = {4, 5, 6, 7, 8}, co[7], cref[7];
        for (int i = 0; i < 7; i++) {
            int32_t s = 0;
            for (int t = 0; t < 3; t++) { int j = i - t; if (j >= 0 && j < 5) s += hh[t] * xx[j]; }
            cref[i] = (int16_t)s;
        }
        int crn = conv_run(dev, hh, 3, xx, 5, co, 7, err, sizeof(err));
        int ok = (crn == 7) && memcmp(co, cref, sizeof cref) == 0;
        printf("%-5s conv 3*5\n", ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    return fails;
}

static int selftest(void)
{
    cgra_t *dev = cgra_open("sim:", 115200);
    if (!dev) { fprintf(stderr, "cgra: cannot open emulator\n"); return 1; }

    dsl_ctx d;
    dsl_init(&d);
    char err[DSL_VAL];
    if (dsl_load_defaults(&d, err, sizeof(err)) != 0) {
        fprintf(stderr, "cgra: %s\n", err); cgra_close(dev); return 1;
    }
    int fails = device_checks(dev, &d);
    cgra_close(dev);

    /* auto-retry path: sim:flaky NACKs the first CFG and WR; the result must
     * still be correct and the stats must show the retries. */
    cgra_t *fdev = cgra_open("sim:flaky", 115200);
    if (fdev) {
        int16_t fa[4] = {1, 2, 3, 4}, fb[4] = {10, 20, 30, 40}, fo[4];
        int rc = cgra_vec_add(fdev, fa, fb, fo, 4);
        cgra_stats_t s; cgra_get_stats(fdev, &s);
        int ok = (rc == CGRA_OK) && fo[0] == 11 && fo[3] == 44 && s.retries == 2;
        printf("%-5s retry-on-nack (retries=%lu)\n", ok ? "ok" : "FAIL", s.retries);
        if (!ok) fails++;
        cgra_close(fdev);
    }

    printf(fails ? "%d test(s) FAILED\n" : "all tests PASSED\n", fails);
    return fails ? 1 : 0;
}

static void save_device_state(const char *port)
{
    char udir[DSL_VAL], p[DSL_VAL + 16];
    if (dsl_user_dir(udir, sizeof(udir)) != 0)
        return;
    mkdir_p(udir);
    if (device_state_path(p, sizeof(p)) != 0)
        return;
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s\n", port); fclose(f); }
}

/* Scan the common USB-serial device paths, ping each, and report any CGRA
 * found; the first is recorded so later commands can use `-d auto`. */
static int probe_scan(void)
{
    static const char *pats[] = { "/dev/ttyUSB*", "/dev/ttyACM*" };
    char first[DSL_VAL] = {0};
    int found = 0;

    for (size_t p = 0; p < sizeof(pats) / sizeof(pats[0]); p++) {
        glob_t g;
        if (glob(pats[p], 0, NULL, &g) != 0)
            continue;
        for (size_t i = 0; i < g.gl_pathc; i++) {
            cgra_t *dev = cgra_open(g.gl_pathv[i], 115200);
            if (!dev)
                continue;
            cgra_set_timeout(dev, 300);
            cgra_info_t info;
            if (cgra_identify(dev, &info) == CGRA_OK) {
                printf("found CGRA v%u on %s (%ux%u, %u-bit)\n",
                       info.version, g.gl_pathv[i], info.rows, info.cols, info.data_w);
                if (!found) { snprintf(first, sizeof(first), "%s", g.gl_pathv[i]); found = 1; }
            }
            cgra_close(dev);
        }
        globfree(&g);
    }

    if (found) {
        save_device_state(first);
        printf("saved %s — use it with `cgra -d auto ...`\n", first);
        return 0;
    }
    fprintf(stderr, "cgra: no CGRA found on /dev/ttyUSB* or /dev/ttyACM*\n");
    return 1;
}

/* Bring-up check: with no device, scan the serial ports; with a device,
 * identify it and run the known-answer tests. */
static int cmd_probe(dsl_ctx *d, const char *devname)
{
    if (devname == NULL && getenv("CGRA_DEVICE") == NULL)
        return probe_scan();

    char err[DSL_VAL];
    cgra_t *dev = open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }

    cgra_info_t info;
    if (cgra_identify(dev, &info) != CGRA_OK) {
        fprintf(stderr, "cgra: no response from device (check wiring / baud)\n");
        cgra_close(dev);
        return 1;
    }
    printf("device: protocol v%u, %ux%u array, %u-bit datapath\n",
           info.version, info.rows, info.cols, info.data_w);
    if (info.rows != CGRA_ROWS || info.cols != CGRA_COLS || info.data_w != CGRA_DATA_W)
        printf("warning: geometry differs from this build (%dx%d, %d-bit)\n",
               CGRA_ROWS, CGRA_COLS, CGRA_DATA_W);

    int fails = device_checks(dev, d);
    cgra_close(dev);
    printf(fails ? "%d check(s) FAILED\n" : "device OK\n", fails);
    return fails ? 1 : 0;
}

/* -------------------------------------------------- usage */

static void usage(void)
{
    fputs(
        "usage: cgra [-d DEVICE] [-c FILE] <command> [args]\n"
        "\n"
        "  modes | devices | pipelines | io | config   list configuration\n"
        "  check                                        validate the loaded configuration\n"
        "  init [--force]                               install defaults to ~/.config/cgra\n"
        "  ping | reset | dump              [-d DEVICE] talk to the device\n"
        "  show MODE [--imm N] [-v]                     print compiled config words\n"
        "  run  MODE [nums...] [--a SRC] [--b SRC] [--imm N] [--steps N] [--io P]\n"
        "  pipe NAME [nums...] [--a SRC] [--b SRC] [--io P]\n"
        "  matvec -m SRC --x SRC [--cols N] [--io P]    systolic y = A * x\n"
        "  conv -m KERNEL --x SIGNAL [--io P]           1-D convolution\n"
        "  scan [nums...] [--a SRC] [--op sum|max|min|prod] [--io P]  prefix scan\n"
        "  bench [MODE] [--size N] [--repeat R]         time a mode on the device\n"
        "  emulate                                      serve a virtual CGRA on a pty\n"
        "  probe                            [-d DEVICE]  no -d: scan ports; with -d: known-answer tests\n"
        "  selftest                                     run kernels on the sim: emulator\n"
        "\n"
        "input SRC: inline \"1 2 3\", \"@file\", or \"-\" for stdin.\n"
        "device: a profile name, a serial path, or \"sim:\" for the emulator.\n"
        "output: --io PROFILE, --json, --dtype u16|s16, -o/--out FILE.\n",
        stderr);
}

/* -------------------------------------------------- main */

int main(int argc, char **argv)
{
    const char *opt_device = NULL, *opt_config = NULL;
    const char *opt_a = NULL, *opt_b = NULL, *opt_io = NULL;
    const char *opt_matrix = NULL, *opt_x = NULL, *opt_outfile = NULL, *opt_op = NULL;
    long opt_imm = 0; int has_imm = 0;
    int opt_steps = 0, has_steps = 0;
    int opt_cols = 0, opt_size = 0, opt_repeat = 0;
    int force = 0, verbose = 0;
    char *pos[64]; int npos = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if ((!strcmp(arg, "-d") || !strcmp(arg, "--device")) && i + 1 < argc) opt_device = argv[++i];
        else if ((!strcmp(arg, "-c") || !strcmp(arg, "--config")) && i + 1 < argc) opt_config = argv[++i];
        else if (!strcmp(arg, "--a") && i + 1 < argc) opt_a = argv[++i];
        else if (!strcmp(arg, "--b") && i + 1 < argc) opt_b = argv[++i];
        else if ((!strcmp(arg, "-m") || !strcmp(arg, "--matrix")) && i + 1 < argc) opt_matrix = argv[++i];
        else if (!strcmp(arg, "--x") && i + 1 < argc) opt_x = argv[++i];
        else if (!strcmp(arg, "--cols") && i + 1 < argc) opt_cols = atoi(argv[++i]);
        else if (!strcmp(arg, "--op") && i + 1 < argc) opt_op = argv[++i];
        else if (!strcmp(arg, "--io") && i + 1 < argc) opt_io = argv[++i];
        else if ((!strcmp(arg, "-o") || !strcmp(arg, "--out")) && i + 1 < argc) opt_outfile = argv[++i];
        else if (!strcmp(arg, "--json")) g_fmt_override = "json";
        else if (!strcmp(arg, "--dtype") && i + 1 < argc) {
            const char *dt = argv[++i];
            g_unsigned = (dt[0] == 'u');
        }
        else if (!strcmp(arg, "--size") && i + 1 < argc) opt_size = atoi(argv[++i]);
        else if (!strcmp(arg, "--repeat") && i + 1 < argc) opt_repeat = atoi(argv[++i]);
        else if (!strcmp(arg, "--imm") && i + 1 < argc) { opt_imm = strtol(argv[++i], NULL, 0); has_imm = 1; }
        else if (!strcmp(arg, "--steps") && i + 1 < argc) { opt_steps = atoi(argv[++i]); has_steps = 1; }
        else if (!strcmp(arg, "--force")) force = 1;
        else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) verbose = 1;
        else if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) { usage(); return 0; }
        else if (npos < 64) pos[npos++] = argv[i];
    }

    if (opt_outfile) {
        g_out = fopen(opt_outfile, "w");
        if (!g_out) { fprintf(stderr, "cgra: cannot write %s: %s\n", opt_outfile, strerror(errno)); return 1; }
    }

    if (npos == 0) { usage(); return 1; }
    const char *cmd = pos[0];
    char **rest = pos + 1;
    int nrest = npos - 1;

    dsl_ctx d;
    dsl_init(&d);
    char err[DSL_VAL];
    if (dsl_load(&d, opt_config, err, sizeof(err)) != 0) {
        fprintf(stderr, "cgra: config error: %s\n", err);
        return 1;
    }
    dsl_io *io = opt_io ? dsl_find_io(&d, opt_io) : dsl_find_io(&d, "dec");

    if (!strcmp(cmd, "devices"))   return cmd_devices(&d);
    if (!strcmp(cmd, "modes"))     return cmd_modes(&d);
    if (!strcmp(cmd, "pipelines")) return cmd_pipelines(&d);
    if (!strcmp(cmd, "io"))        return cmd_io(&d);
    if (!strcmp(cmd, "config"))    return cmd_config(&d);
    if (!strcmp(cmd, "check"))     return cmd_check(&d);
    if (!strcmp(cmd, "init"))      return cmd_init(force);
    if (!strcmp(cmd, "ping"))      return cmd_ping(&d, opt_device);
    if (!strcmp(cmd, "reset"))     return cmd_reset(&d, opt_device);
    if (!strcmp(cmd, "dump"))      return cmd_dump(&d, opt_device, io);
    if (!strcmp(cmd, "selftest"))  return selftest();
    if (!strcmp(cmd, "probe"))     return cmd_probe(&d, opt_device);
    if (!strcmp(cmd, "emulate"))   return cmd_emulate();
    if (!strcmp(cmd, "show")) {
        if (nrest < 1) { fprintf(stderr, "cgra: show needs a MODE\n"); return 1; }
        return cmd_show(&d, rest[0], opt_imm, has_imm, verbose);
    }
    if (!strcmp(cmd, "run")) {
        if (nrest < 1) { fprintf(stderr, "cgra: run needs a MODE\n"); return 1; }
        return cmd_run(&d, opt_device, rest[0], rest + 1, nrest - 1,
                       opt_a, opt_b, opt_imm, has_imm, opt_steps, has_steps, io);
    }
    if (!strcmp(cmd, "pipe")) {
        if (nrest < 1) { fprintf(stderr, "cgra: pipe needs a NAME\n"); return 1; }
        return cmd_pipe(&d, opt_device, rest[0], rest + 1, nrest - 1,
                        opt_a, opt_b, io);
    }
    if (!strcmp(cmd, "matvec"))
        return cmd_matvec(&d, opt_device, opt_matrix ? opt_matrix : opt_a,
                          opt_x ? opt_x : opt_b, opt_cols, io);
    if (!strcmp(cmd, "scan"))
        return cmd_scan(&d, opt_device, rest, nrest, opt_a, opt_op, io);
    if (!strcmp(cmd, "conv"))
        return cmd_conv(&d, opt_device, opt_matrix, opt_x, opt_a, opt_b, io);
    if (!strcmp(cmd, "bench"))
        return cmd_bench(&d, opt_device, nrest > 0 ? rest[0] : NULL, opt_size, opt_repeat);

    fprintf(stderr, "cgra: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
