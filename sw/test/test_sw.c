/*
 * test_sw.c - unit tests for the CLI's DSL parser and mode compiler.
 *
 * assert.h + TAP-style CHECK(); exercises dsl.c and compile.c directly (the
 * compute engines run against the sim: emulator, so no FPGA is needed).
 */

#include "cgra.h"
#include "dsl.h"
#include "compile.h"
#include "kernel_cases.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int t_count = 0, t_fail = 0;

/* Evaluate the condition EXACTLY once: checks routinely wrap a call that talks
 * to the device, and re-evaluating it for the assert would run that
 * transaction a second time (silently doubling any state it advances). */
#define CHECK(cond, msg) do {                             \
        const int ok_ = (cond) ? 1 : 0;                   \
        t_count++;                                        \
        if (ok_) printf("ok %d - %s\n", t_count, (msg));  \
        else { t_fail++; printf("not ok %d - %s\n", t_count, (msg)); } \
        assert(ok_);                                      \
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

static void test_strict_parser(void)
{
    dsl_ctx d, before;
    char err[256];
    dsl_init(&d);
    CHECK(dsl_load_defaults(&d, err, sizeof(err)) == 0, "strict parser defaults");
    before = d;
    const char *invalid[] = {
        "[device sim]\nbaud=115200junk\n",
        "[device sim]\nbaud=-1\n",
        "[device sim]\ntimeout=-1\n",
        "[device sim]\ntimeout=99999999999999999999999999999999\n",
        "[io dec]\nwidth=8\n", "[io dec]\nformat=decimal\n",
        "[io dec]\nsep=typo\n", "[mode add extra]\nop=sub\n",
        "[mode add] trailing\nop=sub\n",
        "[mode add]\nb=const($MISSING)\n",
        "set N=1\n[mode add]\nb=const(${N)\n",
        "set =1\n", "set 9NAME=1\n", "set NAME! = 1\n",
        "[pipeline p]\nstage=addi imm=1 imm=2\n",
        "[mode x]\npattern=custom\npe 0,0junk=op=add\n",
        "[mode x]\npattern=custom\npe 99999999999999999999999999,0=op=add\n",
        "[mode x]\npattern=custom\npe 0,0=\n",
        "[mode x]\npattern=custom\npe 0,0=op=add op=sub\n",
        "[mode x]\nop=abcdefghijklmnopqrstuvwxyz123456\n",
        "[mode abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuv]\n",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(dsl_parse(&d, invalid[i], "<strict>", err, sizeof(err)) != 0 &&
              memcmp(&d, &before, sizeof(d)) == 0,
              "invalid token/value rejected without changing configuration");
    }
    char vars[2048]; size_t pos = 0;
    for (int i = 0; i <= DSL_MAX_VARS; i++)
        pos += (size_t)snprintf(vars + pos, sizeof(vars) - pos, "set V%d=1\n", i);
    CHECK(dsl_parse(&d, vars, "<variables>", err, sizeof(err)) != 0 &&
          memcmp(&d, &before, sizeof(d)) == 0, "variable capacity overflow rolls back");
    CHECK(dsl_parse(&d, "set N=7\n[mode add]\ndoc=$$${N}\n[device sim]\ntimeout=0\n",
                    "<valid>", err, sizeof(err)) == 0 &&
          !strcmp(dsl_find_mode(&d, "add")->doc, "$7") &&
          dsl_find_device(&d, "sim")->timeout_ms == 0,
          "literal dollar, braced variable and explicit zero timeout accepted");
    dsl_mode mode = *dsl_find_mode(&d, "add");
    strcpy(mode.pattern, "custom");
    mode.op[0] = mode.a[0] = mode.b[0] = '\0';
    uint32_t cfg[CGRA_MAX_PE];
    const char *outputs[] = {"garbage", "pe0,0", "pe 0,0junk", "pe 0,9999999999999999999999"};
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        snprintf(mode.out, sizeof(mode.out), "%s", outputs[i]);
        CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) != 0,
              "invalid output selector is not silently ignored");
    }
}

static void test_validation_regressions(void)
{
    dsl_ctx d;
    char err[256];
    uint32_t cfg[CGRA_MAX_PE];
    dsl_init(&d);
    CHECK(dsl_load_defaults(&d, err, sizeof(err)) == 0, "validation defaults");
    CHECK(dsl_parse(&d, "[mode add]\n\nop=mul\nbogus=1\n", "<rollback>", err, sizeof(err)) != 0,
          "malformed override rejected");
    CHECK(strcmp(dsl_find_mode(&d, "add")->op, "add") == 0,
          "failed override leaves the original mode intact");
    CHECK(strstr(err, "<rollback>:4:") != NULL, "diagnostic counts blank lines");
    CHECK(dsl_parse(&d, "[mode typo]\npattern=custom\npe 0,0=opp=add a=north b=west\n",
                    "<pe>", err, sizeof(err)) != 0, "unknown PE field rejected");
    CHECK(dsl_find_mode(&d, "typo") == NULL, "failed parse creates no partial mode");
    CHECK(dsl_parse(&d, "[pipeline bad]\nstage=addi imm=4junk\n",
                    "<stage>", err, sizeof(err)) != 0, "stage immediate requires complete integer");

    dsl_mode mode = *dsl_find_mode(&d, "add");
    const char *bad_steps[] = {"-1", "256", "1junk", "999999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad_steps) / sizeof(bad_steps[0]); i++) {
        snprintf(mode.steps, sizeof(mode.steps), "%s", bad_steps[i]);
        CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) != 0, "invalid step count rejected");
    }
    strcpy(mode.steps, "0");
    CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) == 0, "explicit zero steps is valid");
    strcpy(mode.steps, "255");
    CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) == 0, "255 steps is valid");
    const char *bad_const[] = {"const()", "const(1", "const(1)junk", "const(65536)", "const($MISSING)"};
    for (size_t i = 0; i < sizeof(bad_const) / sizeof(bad_const[0]); i++) {
        snprintf(mode.a, sizeof(mode.a), "%s", bad_const[i]);
        CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) != 0, "malformed immediate rejected");
    }
    strcpy(mode.a, "const(2)"); strcpy(mode.b, "const(3)");
    CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) != 0, "two different diagonal immediates rejected");
    CHECK(mode_compile(&mode, 7, 1, cfg, err, sizeof(err)) == 0, "override sets both constant operands");
    strcpy(mode.pattern, "reduce");
    CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) != 0, "two different reduction immediates rejected");
    strcpy(mode.pattern, "custom"); mode.npe = 1;
    mode.op[0] = mode.a[0] = mode.b[0] = '\0';
    strcpy(mode.pe[0].op, "add"); strcpy(mode.pe[0].a, "const(2)"); strcpy(mode.pe[0].b, "const(3)");
    CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) != 0, "two different custom immediates rejected");
    strcpy(mode.pe[0].a, "const(-1)"); strcpy(mode.pe[0].b, "const(65535)");
    CHECK(mode_compile(&mode, 0, 0, cfg, err, sizeof(err)) == 0, "equivalent signed and unsigned immediates accepted");

    CHECK(dsl_parse(&d, "[pipeline missing_b]\nstage=addi imm=1\nstage=mul\n",
                    "<pipeline>", err, sizeof(err)) == 0, "pipeline syntax parses before semantic validation");
    const cgra_info_t geometry = {3, 4, 4, 16};
    CHECK(pipeline_validate(&d, dsl_find_pipeline(&d, "missing_b"), geometry, err, sizeof(err)) == CGRA_ERR_ARG,
          "binary later pipeline stage rejected");

    cgra_t *dev = cgra_open("sim:", 115200);
    CHECK(dev != NULL, "open validation device");
    if (!dev) return;
    cgra_get_info(dev, NULL);
    cgra_reset_stats(dev);
    int16_t a[2] = {1, 2}, b[2] = {3, 4}, out[2] = {-9, -9};
    CHECK(mode_run(dev, dsl_find_mode(&d, "dot"), a, b, 2, 0, 0, out, 0, err, sizeof(err)) == CGRA_ERR_ARG,
          "reduction capacity rejected before execution");
    CHECK(mode_run(dev, dsl_find_mode(&d, "add"), a, NULL, 2, 0, 0, out, 2, err, sizeof(err)) == CGRA_ERR_ARG,
          "missing B rejected by the C engine");
    CHECK(conv_run(dev, a, INT_MAX, b, 2, out, 2, err, sizeof(err)) == CGRA_ERR_ARG,
          "convolution length overflow rejected before arithmetic");
    CHECK(scan_run(dev, a, 2, CGRA_OP_SUB, out, err, sizeof(err)) == CGRA_ERR_ARG,
          "unsupported scan operation rejected instead of becoming ADD");
    cgra_stats_t stats;
    cgra_get_stats(dev, &stats);
    CHECK(stats.transactions == 0 && out[0] == -9 && out[1] == -9,
          "invalid engine arguments leave device and output untouched");
    cgra_close(dev);
}

static void test_operand_bindings(void)
{
    dsl_ctx d;
    char err[256];
    dsl_init(&d);
    CHECK(dsl_load_defaults(&d, err, sizeof(err)) == 0, "binding defaults");
    CHECK(dsl_parse_file(&d, "config/stdlib/arithmetic.cgra", err, sizeof(err)) == 0,
          "load actual arithmetic standard library");
    CHECK(dsl_parse(&d, "[mode swapped]\nop=sub\na=west\nb=north\n"
                       "[mode sum_b]\npattern=reduce\nop=acc\na=west\nb=zero\n",
                    "<binding>", err, sizeof(err)) == 0, "parse reversed operands");
    const int16_t a[] = {1, -2, 32767, -32768, 0, 5, -7};
    const int16_t b[] = {10, 20, -10, 30, -40, 0, 2};
    int16_t out[7], neg[7], square[7], swapped[7], sum = 0;
    for (size_t i = 0; i < 7; i++) {
        neg[i] = (int16_t)-(int32_t)a[i];
        square[i] = (int16_t)((int32_t)a[i] * a[i]);
        swapped[i] = (int16_t)(b[i] - a[i]);
        sum = (int16_t)(sum + b[i]);
    }
    const char *backends[] = {"sim:", "sim:v2", "sim:2x3", "sim:2x3:v2", "sim:3x2", "sim:1x16"};
    for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
        cgra_t *dev = cgra_open(backends[i], 115200);
        CHECK(dev != NULL, backends[i]);
        if (!dev) continue;
        CHECK(mode_run(dev, dsl_find_mode(&d, "neg"), a, NULL, 7, 0, 0, out, 7, err, sizeof(err)) == 7 &&
              memcmp(out, neg, sizeof(neg)) == 0, "stdlib neg uses A even in ALU operand B");
        CHECK(mode_run(dev, dsl_find_mode(&d, "square"), a, NULL, 7, 0, 0, out, 7, err, sizeof(err)) == 7 &&
              memcmp(out, square, sizeof(square)) == 0, "both north operands read A");
        CHECK(mode_run(dev, dsl_find_mode(&d, "swapped"), a, b, 7, 0, 0, out, 7, err, sizeof(err)) == 7 &&
              memcmp(out, swapped, sizeof(swapped)) == 0, "swapped subtraction reads B minus A");
        CHECK(mode_run(dev, dsl_find_mode(&d, "sum_b"), a, b, 7, 0, 0, out, 7, err, sizeof(err)) == 1 && out[0] == sum,
              "reduction injects B into ALU operand A");
        cgra_close(dev);
    }
}

static void test_stateful_reset(void)
{
    const char *backends[] = {"sim:1x1", "sim:1x1:v2", "sim:2x3", "sim:2x3:v2", "sim:3x2", "sim:3x2:v2"};
    const int16_t a[] = {1, 2, 3, 4, 5, 6}, b[] = {10, 20, 30, 40, 50, 60};
    const int16_t prefix[] = {1, 3, 6, 10, 15, 21};
    char err[256];
    for (size_t k = 0; k < sizeof(backends) / sizeof(backends[0]); k++) {
        cgra_t *dev = cgra_open(backends[k], 115200);
        CHECK(dev != NULL, "open reset semantics device");
        cgra_info_t g;
        CHECK(cgra_get_info(dev, &g) == CGRA_OK, "identify reset semantics device");
        uint32_t seed[CGRA_MAX_PE];
        for (size_t i = 0; i < (size_t)g.rows * g.cols; i++)
            seed[i] = CGRA_CFG(CGRA_OP_CONST, CGRA_SEL_CONST, CGRA_SEL_ZERO, 123);
        CHECK(cgra_configure_n(dev, seed, (size_t)g.rows * g.cols) == CGRA_OK &&
              cgra_run(dev, 1) == CGRA_OK, "seed stale registers before mode invocation");
        dsl_mode m = {0};
        strcpy(m.pattern, "custom"); strcpy(m.out, "pe 0,0"); strcpy(m.steps, "1");
        m.npe = 1; strcpy(m.pe[0].op, "acc"); strcpy(m.pe[0].a, "north");
        int16_t out[6], regs[CGRA_MAX_PE];
        CHECK(mode_run(dev, &m, a, NULL, 6, 0, 0, out, 6, err, sizeof(err)) == 6 &&
              memcmp(out, prefix, sizeof(out)) == 0, "default once clears stale state and accumulates across chunks");
        strcpy(m.reset, "once");
        CHECK(mode_run(dev, &m, a, NULL, 6, 0, 0, out, 6, err, sizeof(err)) == 6 &&
              memcmp(out, prefix, sizeof(out)) == 0, "explicit once starts a new invocation from zero");
        CHECK(cgra_read_regs_n(dev, regs, CGRA_MAX_PE) == CGRA_OK,
              "read full state after reset once");
        int clean = 1;
        for (size_t i = 1; i < (size_t)g.rows * g.cols; i++) if (regs[i]) clean = 0;
        CHECK(clean, "initial reset also clears untapped NOP registers");
        strcpy(m.reset, "each");
        CHECK(mode_run(dev, &m, a, NULL, 6, 0, 0, out, 6, err, sizeof(err)) == 6 &&
              memcmp(out, a, sizeof(out)) == 0, "each clears the accumulator for every chunk");
        if (g.rows > 1 && g.cols > 1) {
            memset(&m, 0, sizeof(m)); strcpy(m.steps, "1");
            const int16_t delayed[] = {11, 0, 33, 22, 55, 44};
            CHECK(mode_run(dev, &m, a, b, 6, 0, 0, out, 6, err, sizeof(err)) == 6 &&
                  memcmp(out, delayed, sizeof(out)) == 0,
                  "underscheduled diagonal retains routing state between chunks on v2/v3");
            strcpy(m.steps, "0");
            CHECK(mode_run(dev, &m, a, b, 6, 0, 0, out, 6, err, sizeof(err)) == 6 &&
                  memcmp(out, (int16_t[6]){0}, sizeof(out)) == 0,
                  "zero steps returns cleared registers even after a previous execution");
        }
        cgra_close(dev);
    }
}

static void test_pattern_fields(void)
{
    const char *invalid[] = {
        "pattern=scan\nop=add\n", "pattern=conv\na=north\n",
        "pattern=systolic\nb=west\n", "pattern=scan\nsteps=auto\n",
        "pattern=conv\nreset=once\n", "pattern=systolic\nout=diag\n",
        "pattern=scan\npe 0,0=op=add\n", "pattern=diagonal\npe 0,0=op=add\n",
        "pattern=custom\nop=add\n", "pattern=custom\na=north\n",
        "pattern=custom\nb=west\n", "pattern=reduce\nout=diag\n",
        "pattern=reduce\nreset=each\n"
    };
    char text[256], err[256];
    cgra_t *dev = cgra_open("sim:", 115200);
    CHECK(dev && cgra_get_info(dev, NULL) == CGRA_OK, "open pattern validation device");
    cgra_reset_stats(dev);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        dsl_ctx d;
        dsl_init(&d);
        snprintf(text, sizeof(text), "[mode invalid]\n%s", invalid[i]);
        CHECK(dsl_parse(&d, text, "<fields>", err, sizeof(err)) == 0,
              "field syntax remains independent of final merged pattern");
        uint32_t cfg[CGRA_MAX_PE]; int16_t a = 1, out = 999;
        dsl_mode *m = dsl_find_mode(&d, "invalid");
        CHECK(mode_compile(m, 0, 0, cfg, err, sizeof(err)) != 0 &&
              mode_run(dev, m, &a, &a, 1, 0, 0, &out, 1, err, sizeof(err)) == CGRA_ERR_ARG &&
              strstr(err, "not applicable") && out == 999,
              "compile and execution reject an ignored field without changing output");
    }
    cgra_stats_t stats;
    cgra_get_stats(dev, &stats);
    CHECK(stats.transactions == 0, "inapplicable fields issue no configuration/reset/run commands");
    cgra_close(dev);
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

/* -------------------------------------------------- engine wire cost */

/* The engines must produce identical results on a v3 device and on one that
 * only speaks v2, and the v3 path must cost strictly fewer round trips. */
static void test_engine_protocol_parity(void)
{
    char err[256];
    enum { M = 9, N = 7, SN = 21 };
    int16_t A[M * N], x[N], y3[M], y2[M];
    int16_t sa[SN], s3[SN], s2[SN];
    cgra_stats_t st3, st2;

    cgra_t *d3 = cgra_open("sim:", 115200);
    cgra_t *d2 = cgra_open("sim:v2", 115200);
    CHECK(d3 != NULL && d2 != NULL, "open both protocol backends");
    if (!d3 || !d2) { if (d3) cgra_close(d3); if (d2) cgra_close(d2); return; }

    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) A[i * N + j] = (int16_t)((i * 3 + j) % 11 - 5);
    for (int j = 0; j < N; j++) x[j] = (int16_t)(j - 2);
    for (int i = 0; i < SN; i++) sa[i] = (int16_t)(i % 5 - 2);

    CHECK(matvec_run(d3, A, M, N, x, y3, err, sizeof(err)) == CGRA_OK, "matvec v3 ok");
    CHECK(matvec_run(d2, A, M, N, x, y2, err, sizeof(err)) == CGRA_OK, "matvec v2 ok");
    CHECK(memcmp(y3, y2, sizeof y3) == 0, "matvec identical across protocols");

    cgra_reset_stats(d3);
    cgra_reset_stats(d2);
    CHECK(scan_run(d3, sa, SN, CGRA_OP_ADD, s3, err, sizeof(err)) == SN, "scan v3 ok");
    CHECK(scan_run(d2, sa, SN, CGRA_OP_ADD, s2, err, sizeof(err)) == SN, "scan v2 ok");
    CHECK(memcmp(s3, s2, sizeof s3) == 0, "scan identical across protocols");
    cgra_get_stats(d3, &st3);
    cgra_get_stats(d2, &st2);
    CHECK(st3.transactions < st2.transactions, "scan: fused path uses fewer round trips");
    CHECK(st3.tx_bytes + st3.rx_bytes < st2.tx_bytes + st2.rx_bytes,
          "scan: fused path moves fewer bytes");

    cgra_close(d3);
    cgra_close(d2);
}

/* conv builds a Toeplitz matrix that is mostly zeros; the engine must skip the
 * all-zero 4x4 blocks entirely rather than streaming them over the link. */
static void test_sparse_skip(void)
{
    char err[256];
    enum { K = 3, XN = 48, ON = XN + K - 1 };
    int16_t h[K] = {1, 2, 3}, x[XN], out[ON], ref[ON];
    cgra_stats_t sparse, dense;

    for (int i = 0; i < XN; i++) x[i] = (int16_t)(i % 9 - 4);
    for (int i = 0; i < ON; i++) {
        int32_t s = 0;
        for (int k = 0; k < K; k++) { int j = i - k; if (j >= 0 && j < XN) s += h[k] * x[j]; }
        ref[i] = (int16_t)s;
    }

    cgra_t *dev = cgra_open("sim:", 115200);
    CHECK(dev != NULL, "open sim: for the sparse-skip test");
    if (!dev) return;

    cgra_reset_stats(dev);
    CHECK(conv_run(dev, h, K, x, XN, out, ON, err, sizeof(err)) == ON, "conv 3*48 runs");
    CHECK(memcmp(out, ref, sizeof ref) == 0, "conv still matches the host reference");
    cgra_get_stats(dev, &sparse);

    /* Same shape, but a dense matrix: nothing can be skipped, so this is the
     * cost the banded case is being compared against. */
    static int16_t A[ON * XN];
    int16_t y[ON];
    for (int i = 0; i < ON * XN; i++) A[i] = 1;
    cgra_reset_stats(dev);
    CHECK(matvec_run(dev, A, ON, XN, x, y, err, sizeof(err)) == CGRA_OK, "dense matvec runs");
    cgra_get_stats(dev, &dense);

    CHECK(sparse.transactions * 4 < dense.transactions,
          "conv skips the Toeplitz zero blocks (>4x fewer round trips)");
    cgra_close(dev);
}

static void test_runtime_geometry(void)
{
    const char *shapes[] = {"1x1", "1x16", "16x1", "2x3", "3x2", "2x8", "8x2", "4x4"};
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        for (int v = 2; v <= 3; v++) {
            char path[32];
            snprintf(path, sizeof(path), "sim:%s:v%d", shapes[i], v);
            /* v3 is the default; only v2 needs an explicit backend token. */
            if (v == 3) snprintf(path, sizeof(path), "sim:%s", shapes[i]);
            cgra_t *dev = cgra_open(path, 115200);
            CHECK(dev != NULL, path);
            CHECK(kernel_cases(dev) == 0, "rectangular kernels match scalar reference");
            cgra_close(dev);
        }
    }
    dsl_ctx ctx;
    char err[256];
    uint32_t cfg[CGRA_MAX_PE];
    dsl_init(&ctx);
    CHECK(dsl_parse(&ctx, "[mode edge]\npattern = custom\npe 0,15 = op=const a=const(7) b=zero\nout = pe 0,15\n",
                    "<geometry>", err, sizeof(err)) == 0, "parse custom coordinate beyond default 4x4");
    dsl_mode *mode = dsl_find_mode(&ctx, "edge");
    const cgra_info_t row = {3, 1, 16, 16}, rect = {3, 2, 3, 16};
    CHECK(mode_compile_for_geometry(mode, 0, 0, row, cfg, CGRA_MAX_PE, err, sizeof(err)) == 0,
          "custom coordinate valid on 1x16");
    CHECK(mode_compile_for_geometry(mode, 0, 0, rect, cfg, CGRA_MAX_PE, err, sizeof(err)) != 0,
          "custom coordinate rejected on 2x3");
    mode->pe[0].c = 0;
    CHECK(mode_compile_for_geometry(mode, 0, 0, rect, cfg, CGRA_MAX_PE, err, sizeof(err)) != 0,
          "out-of-range output tap rejected");
}

int main(void)
{
    printf("# cgra CLI unit tests (DSL + compiler)\n");
    test_parse();
    test_parse_errors();
    test_defaults();
    test_strict_parser();
    test_validation_regressions();
    test_operand_bindings();
    test_stateful_reset();
    test_pattern_fields();
    test_compile();
    test_engines();
    test_engine_protocol_parity();
    test_sparse_skip();
    test_runtime_geometry();

    printf("1..%d\n", t_count);
    printf("# %d passed, %d failed\n", t_count - t_fail, t_fail);
    return t_fail ? 1 : 0;
}
