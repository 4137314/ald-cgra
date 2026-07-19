/*
 * test_sw.c - unit tests for the CLI's DSL parser and mode compiler.
 *
 * assert.h + TAP-style CHECK(); exercises dsl.c and compile.c directly (the
 * compute engines run against the sim: emulator, so no FPGA is needed).
 */

#include "cgra.h"
#include "dsl.h"
#include "compile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int t_count = 0, t_fail = 0;

#define CHECK(cond, msg) do {                             \
        t_count++;                                        \
        if (cond) printf("ok %d - %s\n", t_count, (msg)); \
        else { t_fail++; printf("not ok %d - %s\n", t_count, (msg)); } \
        assert(cond);                                     \
    } while (0)

/* -------------------------------------------------- DSL parsing */

static const char *CFG_TEXT =
    "[device lab]\n"
    "port = /dev/ttyUSB9\n"
    "baud = 57600\n"
    "\n"
    "set GAIN = 7\n"
    "[mode amplify]\n"
    "doc     = out = a * GAIN\n"
    "pattern = diagonal\n"
    "op      = mul\n"
    "a       = north\n"
    "b       = const($GAIN)\n"
    "\n"
    "[pipeline chain]\n"
    "stage = amplify\n"
    "stage = amplify\n"
    "\n"
    "[io hx]\n"
    "format = hex\n";

static void test_parse(void)
{
    dsl_ctx d;
    dsl_init(&d);
    char err[256];
    int rc = dsl_parse(&d, CFG_TEXT, "<test>", err, sizeof(err));
    CHECK(rc == 0, "dsl_parse accepts a valid config");

    dsl_device *dev = dsl_find_device(&d, "lab");
    CHECK(dev != NULL, "device 'lab' found");
    CHECK(dev && strcmp(dev->port, "/dev/ttyUSB9") == 0, "device port parsed");
    CHECK(dev && dev->baud == 57600, "device baud parsed");

    dsl_mode *m = dsl_find_mode(&d, "amplify");
    CHECK(m != NULL, "mode 'amplify' found");
    CHECK(m && strcmp(m->op, "mul") == 0, "mode op parsed");
    CHECK(m && strcmp(m->b, "const(7)") == 0, "variable $GAIN expanded to 7");

    dsl_pipeline *p = dsl_find_pipeline(&d, "chain");
    CHECK(p != NULL && p->nstage == 2, "pipeline 'chain' has two stages");

    CHECK(dsl_find_io(&d, "hx") != NULL, "io profile 'hx' found");
}

static void test_parse_errors(void)
{
    dsl_ctx d;
    char err[256];

    dsl_init(&d);
    CHECK(dsl_parse(&d, "[mode x]\nbogus = 1\n", "<e>", err, sizeof(err)) != 0,
          "unknown directive is rejected");

    dsl_init(&d);
    CHECK(dsl_parse(&d, "key = value\n", "<e>", err, sizeof(err)) != 0,
          "directive outside any section is rejected");

    dsl_init(&d);
    CHECK(dsl_parse(&d, "[bad name]\n", "<e>", err, sizeof(err)) != 0,
          "unknown section type is rejected");
}

static void test_defaults(void)
{
    dsl_ctx d;
    dsl_init(&d);
    char err[256];
    CHECK(dsl_load_defaults(&d, err, sizeof(err)) == 0, "built-in defaults parse");
    CHECK(dsl_find_mode(&d, "add") && dsl_find_mode(&d, "dot") &&
          dsl_find_mode(&d, "matvec"), "core modes present in defaults");
    CHECK(dsl_find_device(&d, "sim") != NULL, "sim device in defaults");
}

/* -------------------------------------------------- compilation */

static void test_compile(void)
{
    dsl_ctx d;
    dsl_init(&d);
    char err[256];
    dsl_load_defaults(&d, err, sizeof(err));

    dsl_mode *add = dsl_find_mode(&d, "add");
    uint32_t cfg[CGRA_NUM_PE];
    CHECK(mode_compile(add, 0, 0, cfg, err, sizeof(err)) == 0, "compile 'add' ok");

    /* diagonal PE(0,0) must be ADD(north, west). */
    uint32_t w = cfg[0];
    CHECK(((w >> 16) & 0xF) == CGRA_OP_ADD,  "PE(0,0) opcode = ADD");
    CHECK(((w >> 20) & 0x7) == CGRA_SEL_N,   "PE(0,0) sel_a = north");
    CHECK(((w >> 23) & 0x7) == CGRA_SEL_W,   "PE(0,0) sel_b = west");

    char dec[64];
    cfg_decode(w, dec, sizeof(dec));
    CHECK(strncmp(dec, "add", 3) == 0, "cfg_decode names the opcode");
}

/* -------------------------------------------------- engines (via sim:) */

static void test_engines(void)
{
    cgra_t *dev = cgra_open("sim:", 115200);
    CHECK(dev != NULL, "open sim: for engine tests");
    if (!dev) return;
    char err[256];

    /* systolic matrix-vector, non-square to exercise tiling. */
    enum { M = 6, N = 5 };
    int16_t A[M * N], x[N], y[M], yref[M];
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) A[i * N + j] = (int16_t)(i - j + 1);
    for (int j = 0; j < N; j++) x[j] = (int16_t)(j + 1);
    for (int i = 0; i < M; i++) {
        int32_t s = 0;
        for (int j = 0; j < N; j++) s += A[i * N + j] * x[j];
        yref[i] = (int16_t)s;
    }
    CHECK(matvec_run(dev, A, M, N, x, y, err, sizeof(err)) == CGRA_OK, "matvec_run ok");
    CHECK(memcmp(y, yref, sizeof yref) == 0, "matvec matches host reference (6x5)");

    /* inclusive prefix scan across two blocks. */
    int16_t sa[9], so[9], sref[9];
    int32_t run = 0;
    for (int i = 0; i < 9; i++) { sa[i] = (int16_t)(i - 3); run += sa[i]; sref[i] = (int16_t)run; }
    CHECK(scan_run(dev, sa, 9, CGRA_OP_ADD, so, err, sizeof(err)) == 9, "scan_run returns n");
    CHECK(memcmp(so, sref, sizeof sref) == 0, "scan matches host reference");

    /* full 1-D convolution. */
    int16_t h[3] = {1, 2, 3}, xx[5] = {4, 5, 6, 7, 8}, co[7], cref[7];
    for (int i = 0; i < 7; i++) {
        int32_t s = 0;
        for (int k = 0; k < 3; k++) { int j = i - k; if (j >= 0 && j < 5) s += h[k] * xx[j]; }
        cref[i] = (int16_t)s;
    }
    CHECK(conv_run(dev, h, 3, xx, 5, co, 7, err, sizeof(err)) == 7, "conv_run returns n+k-1");
    CHECK(memcmp(co, cref, sizeof cref) == 0, "conv matches host reference");

    cgra_close(dev);
}

int main(void)
{
    printf("# cgra CLI unit tests (DSL + compiler)\n");
    test_parse();
    test_parse_errors();
    test_defaults();
    test_compile();
    test_engines();

    printf("1..%d\n", t_count);
    printf("# %d passed, %d failed\n", t_count - t_fail, t_fail);
    return t_fail ? 1 : 0;
}
