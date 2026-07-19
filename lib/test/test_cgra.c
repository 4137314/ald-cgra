/*
 * test_cgra.c - unit tests for libcgra, exercised against the sim: emulator.
 *
 * assert.h for hard invariants; a TAP-style CHECK() for structured output
 * ("ok N - ..."). Exit status is non-zero if any check fails. No FPGA needed.
 */

#include "cgra.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int t_count = 0, t_fail = 0;

#define CHECK(cond, msg) do {                                   \
        t_count++;                                              \
        if (cond) {                                             \
            printf("ok %d - %s\n", t_count, (msg));             \
        } else {                                                \
            t_fail++;                                           \
            printf("not ok %d - %s\n", t_count, (msg));         \
        }                                                       \
        assert(cond);                                           \
    } while (0)

/* -------------------------------------------------- config-word packing */

static void test_cfg_word(void)
{
    uint32_t w = CGRA_CFG(CGRA_OP_MAC, CGRA_SEL_N, CGRA_SEL_W, -7);
    CHECK((int16_t)(w & 0xFFFF) == -7,          "CFG imm field round-trips signed");
    CHECK(((w >> 16) & 0xF) == CGRA_OP_MAC,     "CFG opcode field");
    CHECK(((w >> 20) & 0x7) == CGRA_SEL_N,      "CFG sel_a field");
    CHECK(((w >> 23) & 0x7) == CGRA_SEL_W,      "CFG sel_b field");
    CHECK((w >> 26) == 0,                       "CFG reserved bits are zero");
}

/* -------------------------------------------------- connection + identify */

static void test_identify(cgra_t *dev)
{
    cgra_info_t info;
    int rc = cgra_identify(dev, &info);
    CHECK(rc == CGRA_OK,                 "identify succeeds on sim:");
    CHECK(info.version == CGRA_PROTO_VER, "identify reports protocol version");
    CHECK(info.rows == CGRA_ROWS,        "identify reports ROWS");
    CHECK(info.cols == CGRA_COLS,        "identify reports COLS");
    CHECK(info.data_w == CGRA_DATA_W,    "identify reports datapath width");
}

/* -------------------------------------------------- primitives round-trip */

static void test_primitives(cgra_t *dev)
{
    /* PE(0,0) = north + west; PASS not needed, read PE(0,0) after 1 step. */
    uint32_t cfg[CGRA_NUM_PE];
    for (int i = 0; i < CGRA_NUM_PE; i++)
        cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);
    cfg[0] = CGRA_CFG(CGRA_OP_ADD, CGRA_SEL_N, CGRA_SEL_W, 0);

    int16_t west[CGRA_ROWS]  = { 5, 0, 0, 0 };
    int16_t north[CGRA_COLS] = { 37, 0, 0, 0 };
    int16_t regs[CGRA_NUM_PE];

    CHECK(cgra_configure(dev, cfg) == CGRA_OK,                 "configure ok");
    CHECK(cgra_write_inputs(dev, west, north) == CGRA_OK,      "write_inputs ok");
    CHECK(cgra_run(dev, 1) == CGRA_OK,                         "run ok");
    CHECK(cgra_read_regs(dev, regs) == CGRA_OK,               "read_regs ok");
    CHECK(regs[0] == 42,                                       "PE(0,0)=north+west=42");

    CHECK(cgra_reset_datapath(dev) == CGRA_OK,                "reset_datapath ok");
    CHECK(cgra_read_regs(dev, regs) == CGRA_OK,               "read after reset ok");
    CHECK(regs[0] == 0,                                        "registers cleared by reset");
}

/* -------------------------------------------------- element-wise kernels */

static void test_kernels(cgra_t *dev)
{
    enum { N = 10 };
    int16_t a[N], b[N], out[N];
    for (int i = 0; i < N; i++) { a[i] = (int16_t)(i - 4); b[i] = (int16_t)(2 * i + 1); }

    int ok;

    CHECK(cgra_vec_add(dev, a, b, out, N) == CGRA_OK, "vec_add ok");
    ok = 1; for (int i = 0; i < N; i++) ok &= out[i] == (int16_t)(a[i] + b[i]);
    CHECK(ok, "vec_add matches host reference");

    CHECK(cgra_vec_sub(dev, a, b, out, N) == CGRA_OK, "vec_sub ok");
    ok = 1; for (int i = 0; i < N; i++) ok &= out[i] == (int16_t)(a[i] - b[i]);
    CHECK(ok, "vec_sub matches host reference");

    CHECK(cgra_vec_mul(dev, a, b, out, N) == CGRA_OK, "vec_mul ok");
    ok = 1; for (int i = 0; i < N; i++) ok &= out[i] == (int16_t)(a[i] * b[i]);
    CHECK(ok, "vec_mul matches (16-bit wrap)");

    CHECK(cgra_vec_relu(dev, a, out, N) == CGRA_OK, "vec_relu ok");
    ok = 1; for (int i = 0; i < N; i++) ok &= out[i] == (a[i] > 0 ? a[i] : 0);
    CHECK(ok, "vec_relu = max(a,0)");

    CHECK(cgra_vec_addi(dev, a, 100, out, N) == CGRA_OK, "vec_addi ok");
    ok = 1; for (int i = 0; i < N; i++) ok &= out[i] == (int16_t)(a[i] + 100);
    CHECK(ok, "vec_addi = a + imm");

    /* n not a multiple of the 4 lanes exercises the tail chunk. */
    int16_t a3[3] = { -1, 2, -3 }, b3[3] = { 5, 6, 7 }, o3[3];
    CHECK(cgra_vec_max(dev, a3, b3, o3, 3) == CGRA_OK, "vec_max (n=3, partial lane)");
    CHECK(o3[0] == 5 && o3[1] == 6 && o3[2] == 7, "vec_max tail chunk correct");
}

/* -------------------------------------------------- shift (fixed SHL path) */

static void test_shift(cgra_t *dev)
{
    /* PE(0,0) = SHL(north, const(2)); negative operand must not be UB and
     * must wrap two's-complement like the VHDL shift_left. */
    uint32_t cfg[CGRA_NUM_PE];
    for (int i = 0; i < CGRA_NUM_PE; i++)
        cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);
    cfg[0] = CGRA_CFG(CGRA_OP_SHL, CGRA_SEL_N, CGRA_SEL_CONST, 2);
    int16_t west[CGRA_ROWS] = {0}, north[CGRA_COLS] = { -4, 0, 0, 0 }, regs[CGRA_NUM_PE];
    CHECK(cgra_apply(dev, cfg, west, north, 1, regs) == CGRA_OK, "apply(shl) ok");
    CHECK(regs[0] == (int16_t)((uint16_t)(-4) << 2), "SHL of negative wraps (=-16)");
}

/* -------------------------------------------------- reduction */

static void test_dot(cgra_t *dev)
{
    int16_t a[5] = { 1, 2, 3, 4, 5 }, b[5] = { 5, 4, 3, 2, 1 }, r = 0;
    CHECK(cgra_dot(dev, a, b, 5, &r) == CGRA_OK, "dot ok");
    CHECK(r == 5 + 8 + 9 + 8 + 5, "dot product = 35");
}

/* -------------------------------------------------- error handling */

static void test_errors(cgra_t *dev)
{
    int16_t out[4], a[4] = {1,2,3,4};
    CHECK(cgra_configure(NULL, NULL) == CGRA_ERR_ARG,       "configure(NULL) -> ERR_ARG");
    CHECK(cgra_vec_add(dev, a, NULL, out, 4) == CGRA_ERR_ARG, "vec_add(b=NULL) -> ERR_ARG");
    CHECK(cgra_dot(dev, a, NULL, 4, out) == CGRA_ERR_ARG,   "dot(b=NULL) -> ERR_ARG");
    for (int e = 0; e >= CGRA_ERR_ARG; e--)
        CHECK(cgra_strerror(e) != NULL && cgra_strerror(e)[0] != '\0',
              "strerror returns non-empty text");
}

/* -------------------------------------------------- retry / instrumentation */

static void test_retries(void)
{
    cgra_t *dev = cgra_open("sim:flaky", 115200);
    CHECK(dev != NULL, "open sim:flaky");
    if (!dev) return;

    int16_t a[4] = {1,2,3,4}, b[4] = {10,20,30,40}, out[4];
    int rc = cgra_vec_add(dev, a, b, out, 4);
    cgra_stats_t st;
    cgra_get_stats(dev, &st);
    CHECK(rc == CGRA_OK,        "vec_add recovers through the retry path");
    CHECK(out[0] == 11 && out[3] == 44, "flaky result still correct");
    CHECK(st.retries == 2,      "exactly two retries (first CFG + first WR NACKed)");
    CHECK(st.transactions > 0,  "transaction counter advanced");

    cgra_reset_stats(dev);
    cgra_get_stats(dev, &st);
    CHECK(st.transactions == 0 && st.retries == 0, "reset_stats zeroes counters");
    cgra_close(dev);
}

int main(void)
{
    cgra_t *dev = cgra_open("sim:", 115200);
    if (!dev) { fprintf(stderr, "Bail out! cannot open sim: emulator\n"); return 1; }

    printf("# libcgra unit tests\n");
    test_cfg_word();
    test_identify(dev);
    test_primitives(dev);
    test_kernels(dev);
    test_shift(dev);
    test_dot(dev);
    test_errors(dev);
    cgra_close(dev);
    test_retries();

    printf("1..%d\n", t_count);
    printf("# %d passed, %d failed\n", t_count - t_fail, t_fail);
    return t_fail ? 1 : 0;
}
