/*
 * cli.c — command contracts and dispatch for the `cgra` front end.
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

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <glob.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>



#include "cgra.h"
#include "dsl.h"
#include "compile.h"
#include "bench_ref.h"
#include "paths.h"

#include "cli.h"

/* -------------------------------------------------- vector I/O */

/* -------------------------------------------------- device resolution */

/* -------------------------------------------------- listing commands */

static int cmd_devices(dsl_ctx *d, FILE *out)
{
    for (int i = 0; i < d->ndev; i++)
        fprintf(out, "%-12s %s\n", d->dev[i].name, d->dev[i].port);
    return 0;
}

static int cmd_modes(dsl_ctx *d, FILE *out)
{
    for (int i = 0; i < d->nmode; i++) {
        const dsl_mode *m = &d->mode[i];
        fprintf(out, "%-16s %-9s %s\n", m->name,
               m->pattern[0] ? m->pattern : "diagonal", m->doc);
    }
    return 0;
}

static int cmd_pipelines(dsl_ctx *d, FILE *out)
{
    for (int i = 0; i < d->npipe; i++) {
        const dsl_pipeline *p = &d->pipe[i];
        fprintf(out, "%-16s %s\n", p->name, p->doc);
        for (int s = 0; s < p->nstage; s++) {
            if (p->stage[s].has_imm)
                fprintf(out, "    -> %s imm=%ld\n", p->stage[s].mode, p->stage[s].imm);
            else
                fprintf(out, "    -> %s\n", p->stage[s].mode);
        }
    }
    return 0;
}

static int cmd_io(dsl_ctx *d, FILE *out)
{
    for (int i = 0; i < d->nio; i++)
        fprintf(out, "%-10s format=%s width=%d sep=%s\n", d->io[i].name,
               d->io[i].format, d->io[i].width, d->io[i].sep);
    return 0;
}

static int cmd_config(dsl_ctx *d, FILE *out)
{
    char udir[DSL_VAL];
    fprintf(out, "user config dir : %s\n", dsl_user_dir(udir, sizeof(udir)) == 0 ? udir : "(HOME unset)");
    fprintf(out, "loaded files    : %s\n", d->npath ? "" : "(built-in defaults only)");
    for (int i = 0; i < d->npath; i++)
        fprintf(out, "                  %s\n", d->paths[i]);
    fprintf(out, "devices=%d modes=%d pipelines=%d io=%d\n",
           d->ndev, d->nmode, d->npipe, d->nio);
    return 0;
}

static int cmd_check(dsl_ctx *d, FILE *out)
{
    int errors = 0;
    char err[DSL_VAL];
    uint32_t cfg[CGRA_NUM_PE];

    for (int i = 0; i < d->nmode; i++) {
        if (mode_compile(&d->mode[i], 0, 0, cfg, err, sizeof(err)) != 0) {
            fprintf(stderr, "error: mode %s: %s\n", d->mode[i].name, err);
            errors++;
        }
    }
    for (int i = 0; i < d->npipe; i++) {
        dsl_pipeline *p = &d->pipe[i];
        const cgra_info_t geometry = {CGRA_PROTO_VER, CGRA_ROWS, CGRA_COLS, CGRA_DATA_W};
        if (pipeline_validate(d, p, geometry, err, sizeof(err)) != CGRA_OK) {
            fprintf(stderr, "error: %s\n", err);
            errors++;
        }
    }
    if (errors == 0)
        fprintf(out, "ok: %d modes, %d pipelines, %d devices, %d io profiles\n",
               d->nmode, d->npipe, d->ndev, d->nio);
    else
        fprintf(stderr, "%d error(s)\n", errors);
    return errors ? 1 : 0;
}

/* -------------------------------------------------- init */

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char b[4096];
    size_t n;
    int failed = 0;
    while ((n = fread(b, 1, sizeof(b), in)) > 0)
        if (fwrite(b, 1, n, out) != n) { failed = 1; break; }
    if (ferror(in)) failed = 1;
    if (fclose(in) != 0) failed = 1;
    if (fclose(out) != 0) failed = 1;
    return failed ? -1 : 0;
}

/* Copy the shipped stdlib .cgra files into <udir>/stdlib so that
 * `include "linalg.cgra"` works out of the box. Source is the first of
 * $CGRA_STDLIB, the installed share dir, then relative sw/config/stdlib or
 * config/stdlib (when invoked from sw/). */
static int init_stdlib(const char *udir)
{
    const char *cands[4];
    int nc = 0;
    const char *env = getenv("CGRA_STDLIB");
    if (env && *env) cands[nc++] = env;
    cands[nc++] = CGRA_STDLIB_DIR;
    cands[nc++] = "sw/config/stdlib";
    cands[nc++] = "config/stdlib";

    for (int c = 0; c < nc; c++) {
        char pat[DSL_VAL + 16];
        int len = snprintf(pat, sizeof(pat), "%s/*.cgra", cands[c]);
        if (len < 0 || (size_t)len >= sizeof(pat)) {
            fprintf(stderr, "cgra: stdlib path is too long\n"); return 1;
        }
        glob_t g = {0};
        int grc = glob(pat, 0, NULL, &g);
        if (grc != 0 || g.gl_pathc == 0) {
            globfree(&g);
            if ((grc != 0 && grc != GLOB_NOMATCH) || (c == 0 && env && *env)) {
                fprintf(stderr, "cgra: cannot read stdlib from %s\n", cands[c]); return 1;
            }
            continue;
        }
        char sdir[DSL_VAL + 16];
        snprintf(sdir, sizeof(sdir), "%s/stdlib", udir);
        if (cli_mkdir_p(sdir) != 0) {
            fprintf(stderr, "cgra: cannot create %s\n", sdir);
            globfree(&g); return 1;
        }
        int copied = 0;
        for (size_t i = 0; i < g.gl_pathc; i++) {
            const char *base = strrchr(g.gl_pathv[i], '/');
            base = base ? base + 1 : g.gl_pathv[i];
            char dst[2 * DSL_VAL];
            len = snprintf(dst, sizeof(dst), "%s/%s", sdir, base);
            if (len < 0 || (size_t)len >= sizeof(dst) || copy_file(g.gl_pathv[i], dst) != 0) {
                fprintf(stderr, "cgra: failed to install stdlib file %s\n", base);
                globfree(&g); return 1;
            }
            copied++;
        }
        globfree(&g);
        printf("installed %d stdlib file(s) to %s\n", copied, sdir);
        return 0;
    }
    fprintf(stderr, "cgra: stdlib not found; set CGRA_STDLIB to the source library directory\n");
    return 1;
}

static int cmd_init(int force)
{
    char udir[DSL_VAL];
    if (dsl_user_dir(udir, sizeof(udir)) != 0) {
        fprintf(stderr, "cgra: HOME/XDG_CONFIG_HOME not set\n");
        return 1;
    }
    if (cli_mkdir_p(udir) != 0) {
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
    int failed = fputs(dsl_default_text(), f) == EOF;
    if (fclose(f) != 0) failed = 1;
    if (failed) { fprintf(stderr, "cgra: failed to write %s\n", path); return 1; }
    printf("wrote %s\n", path);
    return init_stdlib(udir);
}

/* -------------------------------------------------- device commands */

static int cmd_ping(dsl_ctx *d, const char *devname, FILE *out)
{
    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cgra_info_t info;
    int rc = cgra_identify(dev, &info);
    if (rc == CGRA_OK) {
        fprintf(out, "cgra ok: protocol v%u, %ux%u array, %u-bit datapath\n",
               info.version, info.rows, info.cols, info.data_w);
        if (cgra_get_info(dev, NULL) != CGRA_OK)
            fprintf(stderr, "warning: device geometry/protocol is unsupported by this library\n");
    } else {
        fprintf(stderr, "cgra: ping failed: %s\n", cgra_strerror(rc));
    }
    cgra_close(dev);
    return rc == CGRA_OK ? 0 : 1;
}

static int cmd_reset(dsl_ctx *d, const char *devname)
{
    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int rc = cgra_reset_datapath(dev);
    if (rc != CGRA_OK) fprintf(stderr, "cgra: reset failed: %s\n", cgra_strerror(rc));
    cgra_close(dev);
    return rc == CGRA_OK ? 0 : 1;
}

static int cmd_dump(cli_context *inv, dsl_ctx *d, const char *devname, const dsl_io *io)
{
    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cgra_info_t info;
    int16_t regs[CGRA_MAX_PE];
    int rc = cgra_get_info(dev, &info);
    if (rc == CGRA_OK) rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
    cgra_close(dev);
    if (rc != CGRA_OK) { fprintf(stderr, "cgra: read failed: %s\n", cgra_strerror(rc)); return 1; }
    for (int r = 0; r < info.rows; r++)
        cli_print_vec(inv, &regs[r * info.cols], info.cols, io);
    return 0;
}

/* -------------------------------------------------- show / run / pipe */

static int cmd_show(dsl_ctx *d, const char *devname, const char *modename, long imm, int has_imm, int verbose, FILE *out)
{
    dsl_mode *m = dsl_find_mode(d, modename);
    if (!m) { fprintf(stderr, "cgra: unknown mode '%s'\n", modename); return 1; }
    uint32_t cfg[CGRA_MAX_PE];
    char err[DSL_VAL];
    cgra_info_t g = {CGRA_PROTO_VER, CGRA_ROWS, CGRA_COLS, CGRA_DATA_W};
    if (devname != NULL) {
        cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
        if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
        int rc = cgra_get_info(dev, &g);
        cgra_close(dev);
        if (rc != CGRA_OK) { fprintf(stderr, "cgra: %s\n", cgra_strerror(rc)); return 1; }
    }
    if (mode_compile_for_geometry(m, imm, has_imm, g, cfg, CGRA_MAX_PE, err, sizeof(err)) != 0) {
        fprintf(stderr, "cgra: %s\n", err);
        return 1;
    }
    fprintf(out, "mode %s (pattern %s):\n", m->name, m->pattern[0] ? m->pattern : "diagonal");
    if (verbose) {
        for (int r = 0; r < g.rows; r++)
            for (int c = 0; c < g.cols; c++) {
                char dec[64];
                cfg_decode(cfg[r * g.cols + c], dec, sizeof(dec));
                fprintf(out, "  PE(%d,%d) %08X  %s\n", r, c, cfg[r * g.cols + c], dec);
            }
    } else {
        for (int r = 0; r < g.rows; r++) {
            for (int c = 0; c < g.cols; c++)
                fprintf(out, "  %08X", cfg[r * g.cols + c]);
            fputc('\n', out);
        }
    }
    return 0;
}

/* Collect a[] and b[] from --a/--b, positionals, or stdin. */
static int gather_inputs(const char *opt_a, const char *opt_b,
                         char **pos, int npos,
                         int16_t *a, size_t *na, int16_t *b, size_t *nb,
                         int need_b)
{
    *na = 0; *nb = 0;
    if (opt_b && !need_b) { fprintf(stderr, "cgra: this mode does not consume --b\n"); return -1; }
    if (opt_a) {
        if (cli_read_vec(opt_a, a, MAX_VEC, na) != 0) { fprintf(stderr, "cgra: bad --a vector\n"); return -1; }
    } else if (npos > 0) {
        for (int i = 0; i < npos; i++) {
            size_t k;
            if (cli_parse_ints(pos[i], a + *na, MAX_VEC - *na, &k) != 0) { fprintf(stderr, "cgra: bad input '%s'\n", pos[i]); return -1; }
            *na += k;
        }
    } else {
        if (cli_read_vec("-", a, MAX_VEC, na) != 0) { fprintf(stderr, "cgra: bad stdin vector\n"); return -1; }
    }
    if (opt_b) {
        if (cli_read_vec(opt_b, b, MAX_VEC, nb) != 0) { fprintf(stderr, "cgra: bad --b vector\n"); return -1; }
    }
    if (need_b) {
        if (*nb == 0) { fprintf(stderr, "cgra: this mode needs a second vector (--b)\n"); return -1; }
        if (*nb != *na) { fprintf(stderr, "cgra: --a and --b lengths differ (%zu vs %zu)\n", *na, *nb); return -1; }
    }
    return 0;
}

static int cmd_run(cli_context *inv, dsl_ctx *d, const char *devname, const char *modename,
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
    if (gather_inputs(opt_a, opt_b, pos, npos, a, &na, b, &nb, mode_requires_b(m)) != 0)
        return 1;

    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }

    int nout = mode_run(dev, &local, a, nb ? b : NULL, na, imm, has_imm,
                        out, MAX_VEC, err, sizeof(err));
    cgra_close(dev);
    if (nout < 0) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cli_print_vec(inv, out, (size_t)nout, io);
    return 0;
}

static int cmd_pipe(cli_context *inv, dsl_ctx *d, const char *devname, const char *pipename,
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
    if (gather_inputs(opt_a, opt_b, pos, npos, a, &na, b, &nb, mode_requires_b(m0)) != 0)
        return 1;

    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }

    cgra_info_t geometry;
    int rc = cgra_get_info(dev, &geometry);
    if (rc != CGRA_OK || pipeline_validate(d, pl, geometry, err, sizeof(err)) != CGRA_OK) {
        fprintf(stderr, "cgra: %s\n", rc != CGRA_OK ? cgra_strerror(rc) : err);
        cgra_close(dev);
        return 1;
    }

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
    cli_print_vec(inv, a, na, io);
    return 0;
}

static int cmd_matvec(cli_context *inv, dsl_ctx *d, const char *devname, const char *m_src,
                      const char *x_src, int cols, const dsl_io *io)
{
    if (!m_src) { fprintf(stderr, "cgra: matvec needs a matrix (-m/--matrix)\n"); return 1; }
    if (!x_src) { fprintf(stderr, "cgra: matvec needs a vector (--x)\n"); return 1; }

    static int16_t A[MAX_VEC], x[MAX_VEC], y[MAX_VEC];
    size_t nA, nx;
    if (cli_read_vec(m_src, A, MAX_VEC, &nA) != 0) { fprintf(stderr, "cgra: bad matrix\n"); return 1; }
    if (cli_read_vec(x_src, x, MAX_VEC, &nx) != 0) { fprintf(stderr, "cgra: bad vector\n"); return 1; }

    int N = cols > 0 ? cols : (int)nx;
    if (N <= 0) { fprintf(stderr, "cgra: cannot determine matrix width\n"); return 1; }
    if ((int)nx != N) { fprintf(stderr, "cgra: x length %zu != matrix width %d\n", nx, N); return 1; }
    if (nA % (size_t)N != 0) { fprintf(stderr, "cgra: matrix size %zu not a multiple of width %d\n", nA, N); return 1; }
    int M = (int)(nA / (size_t)N);

    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int rc = matvec_run(dev, A, M, N, x, y, err, sizeof(err));
    cgra_close(dev);
    if (rc != CGRA_OK) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cli_print_vec(inv, y, (size_t)M, io);
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

static int cmd_scan(cli_context *inv, dsl_ctx *d, const char *devname, char **pos, int npos,
                    const char *opt_a, const char *opt_op, const dsl_io *io)
{
    int op = scan_op_code(opt_op);
    if (op < 0) { fprintf(stderr, "cgra: --op must be sum|max|min|prod\n"); return 1; }

    static int16_t a[MAX_VEC], b[MAX_VEC], out[MAX_VEC];
    size_t na, nb;
    if (gather_inputs(opt_a, NULL, pos, npos, a, &na, b, &nb, 0) != 0)
        return 1;

    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int n = scan_run(dev, a, (int)na, op, out, err, sizeof(err));
    cgra_close(dev);
    if (n < 0) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cli_print_vec(inv, out, (size_t)n, io);
    return 0;
}

static int cmd_conv(cli_context *inv, dsl_ctx *d, const char *devname, const char *h_src,
                    const char *x_src, const char *opt_a, const char *opt_b,
                    const dsl_io *io)
{
    const char *hs = h_src ? h_src : opt_a;
    const char *xs = x_src ? x_src : opt_b;
    if (!hs) { fprintf(stderr, "cgra: conv needs kernel coefficients (-m/--matrix or --a)\n"); return 1; }
    if (!xs) { fprintf(stderr, "cgra: conv needs a signal (--x or --b)\n"); return 1; }

    static int16_t h[MAX_VEC], x[MAX_VEC], out[MAX_VEC];
    size_t nh, nx;
    if (cli_read_vec(hs, h, MAX_VEC, &nh) != 0 || nh == 0) { fprintf(stderr, "cgra: bad kernel\n"); return 1; }
    if (cli_read_vec(xs, x, MAX_VEC, &nx) != 0 || nx == 0) { fprintf(stderr, "cgra: bad signal\n"); return 1; }

    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    int n = conv_run(dev, h, (int)nh, x, (int)nx, out, MAX_VEC, err, sizeof(err));
    cgra_close(dev);
    if (n < 0) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cli_print_vec(inv, out, (size_t)n, io);
    return 0;
}

/* Explicit experimental workload; input validation precedes preflight. */
static int cmd_block_dot(cli_context *inv, dsl_ctx *d, const char *device,
                         const char *as, const char *bs, int repeat, const dsl_io *io)
{
    int16_t a[CGRA_BLOCK_WORDS], b[CGRA_BLOCK_WORDS], out[MAX_VEC];
    size_t na, nb;
    if (!as || !bs || cli_read_vec(as, a, CGRA_BLOCK_WORDS, &na) ||
        cli_read_vec(bs, b, CGRA_BLOCK_WORDS, &nb) || na != nb) {
        fprintf(stderr, "cgra: block-dot requires equal --a/--b vectors of at most 1024 words\n");
        return 1;
    }
    if (!repeat) repeat = 1;
    if (repeat > MAX_VEC) {
        fprintf(stderr, "cgra: block-dot --repeat must be at most %d\n", MAX_VEC); return 1;
    }
    uint32_t nonce = 0;
    FILE *random = fopen("/dev/urandom", "rb");
    if (!random) { fprintf(stderr, "cgra: cannot obtain session nonce\n"); return 1; }
    while (!nonce) {
        if (fread(&nonce, sizeof nonce, 1, random) != 1) {
            fclose(random); fprintf(stderr, "cgra: cannot obtain session nonce\n"); return 1;
        }
    }
    if (fclose(random)) { fprintf(stderr, "cgra: nonce source close failed\n"); return 1; }
    char error[DSL_VAL];
    cgra_t *dev = cli_open_device(d, device, error, sizeof error);
    if (!dev) { fprintf(stderr, "cgra: %s\n", error); return 1; }
    int rc = cgra_block_begin(dev, nonce);
    if (rc == CGRA_OK && na) rc = cgra_block_upload(dev, 0, a, na);
    if (rc == CGRA_OK && nb) rc = cgra_block_upload(dev, 1, b, nb);
    for (int i = 0; i < repeat && rc == CGRA_OK; ++i)
        rc = cgra_block_dot(dev, na, &out[i], NULL);
    cgra_close(dev);
    if (rc != CGRA_OK) { fprintf(stderr, "cgra: block-dot: %s\n", cgra_strerror(rc)); return 1; }
    cli_print_vec(inv, out, (size_t)repeat, io);
    return 0;
}

/* -------------------------------------------------- usage */

void cli_usage(FILE *out)
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
        "  block-dot --a A --b B [--repeat R]           experimental resident dot (max 1024)\n"
        "  bench [MODE] [--size N] [--repeat R]         time a mode on the device\n"
        "  benchall [--size N] [--repeat R] [--json]    stress-bench every mode (structured)\n"
        "  emulate [--rows R --cols C --protocol 2|3 --blocks]    serve a virtual CGRA on a pty\n"
        "  probe                            [-d DEVICE]  no -d: scan ports; with -d: known-answer tests\n"
        "  selftest                                     run kernels on the sim: emulator\n"
        "  shell                                        interactive prompt (readline)\n"
        "\n"
        "input SRC: inline \"1 2 3\", \"@file\", or \"-\" for stdin.\n"
        "device: a profile name, a serial path, or \"sim:\" for the emulator.\n"
        "output: vectors accept --io PROFILE, --json, --dtype u16|s16.\n"
        "        benchmarks accept --json; finite results accept -o/--out FILE.\n"
        "note: options may appear anywhere; put bare negative numbers after \"--\"\n"
        "      (or pass them via --a/--b/stdin), e.g. cgra run relu -- -3 -1 0 5.\n",
        out);
}

/* -------------------------------------------------- option parsing (getopt) */

/* Long-only options get codes above the byte range so they never clash with
 * the short options (-d -c -m -o -v -h). */
enum {
    OPT_A = 256, OPT_B, OPT_X, OPT_COLS, OPT_OP, OPT_IO,
    OPT_JSON, OPT_DTYPE, OPT_SIZE, OPT_REPEAT, OPT_IMM, OPT_STEPS, OPT_FORCE, OPT_VERSION, OPT_ROWS, OPT_PROTOCOL, OPT_BLOCKS
};

static const struct option long_opts[] = {
    { "device",  required_argument, NULL, 'd'        },
    { "config",  required_argument, NULL, 'c'        },
    { "matrix",  required_argument, NULL, 'm'        },
    { "out",     required_argument, NULL, 'o'        },
    { "verbose", no_argument,       NULL, 'v'        },
    { "help",    no_argument,       NULL, 'h'        },
    { "version", no_argument,       NULL, OPT_VERSION },
    { "a",       required_argument, NULL, OPT_A      },
    { "b",       required_argument, NULL, OPT_B      },
    { "x",       required_argument, NULL, OPT_X      },
    { "rows",    required_argument, NULL, OPT_ROWS   },
    { "protocol",required_argument, NULL, OPT_PROTOCOL },
    { "blocks",  no_argument,       NULL, OPT_BLOCKS },
    { "cols",    required_argument, NULL, OPT_COLS   },
    { "op",      required_argument, NULL, OPT_OP     },
    { "io",      required_argument, NULL, OPT_IO     },
    { "json",    no_argument,       NULL, OPT_JSON   },
    { "dtype",   required_argument, NULL, OPT_DTYPE  },
    { "size",    required_argument, NULL, OPT_SIZE   },
    { "repeat",  required_argument, NULL, OPT_REPEAT },
    { "imm",     required_argument, NULL, OPT_IMM    },
    { "steps",   required_argument, NULL, OPT_STEPS  },
    { "force",   no_argument,       NULL, OPT_FORCE  },
    { NULL,      0,                 NULL, 0          }
};


/* Command-level contract, checked before config loading, files or device I/O.
 * Input-dependent checks remain in the relevant handler before device open. */
static int validate_command(const char *cmd, int argc, uint64_t used)
{
    static const struct { const char *name; int min, max; const char *options; } commands[] = {
        {"devices", 0, 0, "config out"}, {"modes", 0, 0, "config out"},
        {"pipelines", 0, 0, "config out"}, {"io", 0, 0, "config out"},
        {"config", 0, 0, "config out"}, {"check", 0, 0, "config out"},
        {"init", 0, 0, "force"}, {"ping", 0, 0, "config device out"},
        {"reset", 0, 0, "config device"},
        {"dump", 0, 0, "config device out io json dtype"},
        {"show", 1, 1, "config device out imm verbose"},
        {"run", 1, INT_MAX, "config device out io json dtype a b imm steps"},
        {"pipe", 1, INT_MAX, "config device out io json dtype a b"},
        {"matvec", 0, 0, "config device out io json dtype matrix x a b cols"},
        {"conv", 0, 0, "config device out io json dtype matrix x a b"},
        {"scan", 0, INT_MAX, "config device out io json dtype a op"},
        {"block-dot", 0, 0, "config device out io json dtype a b repeat"},
        {"bench", 0, 1, "config device out json size repeat"},
        {"benchall", 0, 0, "config device out json size repeat"},
        {"probe", 0, 0, "config device"}, {"emulate", 0, 0, "rows cols protocol blocks"},
        {"selftest", 0, 0, ""}, {"shell", 0, 0, "config"}
    };
    size_t i;
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
        if (!strcmp(cmd, commands[i].name)) break;
    if (i == sizeof(commands) / sizeof(commands[0])) {
        fprintf(stderr, "cgra: unknown command '%s'\n", cmd); return 1;
    }
    for (size_t j = 0; long_opts[j].name; ++j) {
        if (!(used & (UINT64_C(1) << j))) continue;
        const char *name = long_opts[j].name;
        const char *p = commands[i].options;
        size_t len = strlen(name);
        int found = 0;
        while (*p) {
            size_t word = strcspn(p, " ");
            if (word == len && !strncmp(p, name, len)) { found = 1; break; }
            p += word;
            if (*p) ++p;
        }
        if (!found) {
            fprintf(stderr, "cgra: %s does not support --%s\n", cmd, name); return 1;
        }
    }
    if (argc < commands[i].min || argc > commands[i].max) {
        fprintf(stderr, "cgra: invalid number of arguments for %s\n", cmd); return 1;
    }
    return 0;
}

/* Compute into a temporary stream first, so --out cannot truncate an input
 * (including a configuration file or stdin redirected from that file). */
static int publish_output(FILE *staged, const char *path)
{
    if (ferror(staged) || fflush(staged) == EOF || fseek(staged, 0, SEEK_SET) != 0) {
        fprintf(stderr, "cgra: failed to stage output\n");
        return 1;
    }
    FILE *dest = fopen(path, "wb");
    if (!dest) {
        fprintf(stderr, "cgra: cannot write %s: %s\n", path, strerror(errno));
        return 1;
    }
    char buffer[4096];
    size_t n;
    int failed = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), staged)) > 0) {
        if (fwrite(buffer, 1, n, dest) != n) { failed = 1; break; }
    }
    if (ferror(staged) || ferror(dest)) failed = 1;
    if (fclose(dest) != 0) failed = 1;
    if (failed) fprintf(stderr, "cgra: failed to write %s\n", path);
    return failed;
}

/*
 * Parse one command line (argv[0] is the program/tool name) with getopt_long
 * and run the requested command. Used for both the one-shot invocation and
 * each line typed at the interactive shell. Returns the command's exit code.
 */
int cli_dispatch(int argc, char **argv, int in_shell)
{
    /* option values come from getopt's optarg (char *), so keep them mutable
     * to stay const-clean when we hand opt_config to the shell's argv. */
    char *opt_device = NULL, *opt_config = NULL;
    char *opt_a = NULL, *opt_b = NULL, *opt_io = NULL;
    char *opt_matrix = NULL, *opt_x = NULL, *opt_outfile = NULL, *opt_op = NULL;
    long opt_imm = 0; int has_imm = 0;
    int opt_steps = 0, has_steps = 0;
    int opt_rows = CGRA_ROWS, opt_protocol = CGRA_PROTO_VER;
    int opt_cols = 0, opt_size = 0, opt_repeat = 0;
    int force = 0, verbose = 0;
    unsigned emulation_flags = 0;
    uint64_t used = 0;

    cli_context invocation = {0};
    cli_context *inv = &invocation;

    optind = 0;             /* glibc: full re-init, so the shell can re-parse */
    opterr = 1;
    int c;
    while ((c = getopt_long(argc, argv, "d:c:m:o:vh", long_opts, NULL)) != -1) {
        for (size_t i = 0; long_opts[i].name; ++i)
            if (c == long_opts[i].val) used |= UINT64_C(1) << i;
        switch (c) {
        case 'd':        opt_device  = optarg; break;
        case 'c':        opt_config  = optarg; break;
        case 'm':        opt_matrix  = optarg; break;
        case 'o':        opt_outfile = optarg; break;
        case 'v':        verbose = 1;          break;
        case 'h':        cli_usage(stdout);              return 0;
        case OPT_BLOCKS: emulation_flags = CGRA_PTY_BLOCKS; break;
        case OPT_VERSION: puts("cgra 0.1.0"); return 0;
        case OPT_A:      opt_a  = optarg;      break;
        case OPT_B:      opt_b  = optarg;      break;
        case OPT_X:      opt_x  = optarg;      break;
        case OPT_ROWS: case OPT_PROTOCOL: {
            long value;
            long low = c == OPT_ROWS ? 1 : 2, high = c == OPT_ROWS ? CGRA_MAX_EDGE : 3;
            if (dsl_integer(optarg, low, high, &value)) {
                fprintf(stderr, "cgra: --%s needs an integer in %ld..%ld\n",
                        c == OPT_ROWS ? "rows" : "protocol", low, high); return 1;
            }
            if (c == OPT_ROWS) opt_rows = (int)value; else opt_protocol = (int)value;
            break;
        }
        case OPT_COLS: case OPT_SIZE: case OPT_REPEAT: {
            long value;
            long max = c == OPT_REPEAT ? INT_MAX : MAX_VEC;
            if (dsl_integer(optarg, 1, max, &value)) {
                fprintf(stderr, "cgra: --%s needs an integer in 1..%ld\n",
                        c == OPT_COLS ? "cols" : c == OPT_SIZE ? "size" : "repeat", max);
                return 1;
            }
            if (c == OPT_COLS) opt_cols = (int)value;
            else if (c == OPT_SIZE) opt_size = (int)value;
            else opt_repeat = (int)value;
            break;
        }
        case OPT_OP:     opt_op     = optarg;  break;
        case OPT_IO:     opt_io     = optarg;  break;
        case OPT_JSON:   inv->format = "json"; break;
        case OPT_DTYPE:
            if (strcmp(optarg, "u16") && strcmp(optarg, "s16")) {
                fprintf(stderr, "cgra: --dtype must be u16 or s16\n"); return 1;
            }
            inv->unsigned_output = !strcmp(optarg, "u16"); break;
        case OPT_IMM:
            if (dsl_integer(optarg, INT16_MIN, UINT16_MAX, &opt_imm) != 0) {
                fprintf(stderr, "cgra: --imm needs an integer in -32768..65535\n");
                return 1;
            }
            has_imm = 1;
            break;
        case OPT_STEPS: {
            long steps;
            if (dsl_integer(optarg, 0, UINT8_MAX, &steps) != 0) {
                fprintf(stderr, "cgra: --steps needs an integer in 0..255\n");
                return 1;
            }
            opt_steps = (int)steps;
            has_steps = 1;
            break;
        }
        case OPT_FORCE:  force = 1;            break;
        case '?':        cli_usage(stderr);              return 1;   /* getopt already complained */
        default:         cli_usage(stderr);              return 1;
        }
    }

    char **pos = argv + optind;      /* command + positional numbers */
    int    npos = argc - optind;
    if (npos == 0) { cli_usage(stderr); return 1; }
    const char *cmd = pos[0];
    char **rest = pos + 1;
    int nrest = npos - 1;

    if (validate_command(cmd, nrest, used)) return 1;
    int vector_args = (!strcmp(cmd, "run") || !strcmp(cmd, "pipe")) ? nrest - 1
                    : !strcmp(cmd, "scan") ? nrest : 0;
    if (opt_a && vector_args > 0) {
        fprintf(stderr, "cgra: positional input and --a are mutually exclusive\n"); return 1;
    }
    if ((opt_matrix && opt_a) || (opt_x && opt_b)) {
        fprintf(stderr, "cgra: choose one spelling for each input (--matrix/--a, --x/--b)\n"); return 1;
    }

    if (!strcmp(cmd, "shell")) {
        if (in_shell) { fprintf(stderr, "cgra: already in a shell\n"); return 1; }
        return cli_cmd_shell(opt_config);
    }

    if (opt_outfile) {
        inv->out = tmpfile();
        if (!inv->out) { fprintf(stderr, "cgra: cannot stage output: %s\n", strerror(errno)); return 1; }
    }

    dsl_ctx d;
    dsl_init(&d);
    char err[DSL_VAL];
    if (dsl_load(&d, opt_config, err, sizeof(err)) != 0) {
        fprintf(stderr, "cgra: config error: %s\n", err);
        if (inv->out) fclose(inv->out);
        return 1;
    }
    dsl_io *io = opt_io ? dsl_find_io(&d, opt_io) : dsl_find_io(&d, "dec");

    if (opt_io && !io) {
        fprintf(stderr, "cgra: unknown I/O profile '%s'\n", opt_io);
        if (inv->out) { fclose(inv->out); inv->out = NULL; }
        return 1;
    }
    FILE *out = inv->out ? inv->out : stdout;
    int rc = 0;
    if      (!strcmp(cmd, "devices"))   rc = cmd_devices(&d, out);
    else if (!strcmp(cmd, "modes"))     rc = cmd_modes(&d, out);
    else if (!strcmp(cmd, "pipelines")) rc = cmd_pipelines(&d, out);
    else if (!strcmp(cmd, "io"))        rc = cmd_io(&d, out);
    else if (!strcmp(cmd, "config"))    rc = cmd_config(&d, out);
    else if (!strcmp(cmd, "check"))     rc = cmd_check(&d, out);
    else if (!strcmp(cmd, "init"))      rc = cmd_init(force);
    else if (!strcmp(cmd, "ping"))      rc = cmd_ping(&d, opt_device, out);
    else if (!strcmp(cmd, "reset"))     rc = cmd_reset(&d, opt_device);
    else if (!strcmp(cmd, "dump"))      rc = cmd_dump(inv, &d, opt_device, io);
    else if (!strcmp(cmd, "selftest"))  rc = cli_selftest();
    else if (!strcmp(cmd, "probe"))     rc = cli_cmd_probe(&d, opt_device);
    else if (!strcmp(cmd, "emulate"))   rc = cli_cmd_emulate((unsigned)opt_rows,
        opt_cols ? (unsigned)opt_cols : CGRA_COLS, (unsigned)opt_protocol, emulation_flags);
    else if (!strcmp(cmd, "show")) {
        if (nrest < 1) { fprintf(stderr, "cgra: show needs a MODE\n"); rc = 1; }
        else rc = cmd_show(&d, opt_device, rest[0], opt_imm, has_imm, verbose, out);
    }
    else if (!strcmp(cmd, "run")) {
        if (nrest < 1) { fprintf(stderr, "cgra: run needs a MODE\n"); rc = 1; }
        else rc = cmd_run(inv, &d, opt_device, rest[0], rest + 1, nrest - 1,
                          opt_a, opt_b, opt_imm, has_imm, opt_steps, has_steps, io);
    }
    else if (!strcmp(cmd, "pipe")) {
        if (nrest < 1) { fprintf(stderr, "cgra: pipe needs a NAME\n"); rc = 1; }
        else rc = cmd_pipe(inv, &d, opt_device, rest[0], rest + 1, nrest - 1,
                           opt_a, opt_b, io);
    }
    else if (!strcmp(cmd, "matvec"))
        rc = cmd_matvec(inv, &d, opt_device, opt_matrix ? opt_matrix : opt_a,
                        opt_x ? opt_x : opt_b, opt_cols, io);
    else if (!strcmp(cmd, "scan"))
        rc = cmd_scan(inv, &d, opt_device, rest, nrest, opt_a, opt_op, io);
    else if (!strcmp(cmd, "conv"))
        rc = cmd_conv(inv, &d, opt_device, opt_matrix, opt_x, opt_a, opt_b, io);
    else if (!strcmp(cmd, "block-dot"))
        rc = cmd_block_dot(inv, &d, opt_device, opt_a, opt_b, opt_repeat, io);
    else if (!strcmp(cmd, "bench"))
        rc = cli_cmd_bench(inv, &d, opt_device, nrest > 0 ? rest[0] : NULL, opt_size, opt_repeat);
    else if (!strcmp(cmd, "benchall"))
        rc = cli_cmd_benchall(inv, &d, opt_device, opt_size, opt_repeat);
    else {
        fprintf(stderr, "cgra: unknown command '%s'\n", cmd);
        cli_usage(stderr);
        rc = 1;
    }

    if (inv->out) {
        if ((rc == 0 || inv->report_ready) && publish_output(inv->out, opt_outfile) != 0) rc = 1;
        if (fclose(inv->out) != 0) rc = 1;
        inv->out = NULL;
    } else if (fflush(stdout) == EOF || ferror(stdout)) {
        fprintf(stderr, "cgra: failed to write standard output\n");
        rc = 1;
    }
    return rc;
}
