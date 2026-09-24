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

/* Evaluate the condition EXACTLY once: checks routinely wrap a call that talks
 * to the device, and re-evaluating it for the assert would run that
 * transaction a second time (silently doubling any state it advances). */
#define CHECK(cond, msg) do {                                   \
        const int ok_ = (cond) ? 1 : 0;                         \
        t_count++;                                              \
        if (ok_) {                                              \
            printf("ok %d - %s\n", t_count, (msg));             \
        } else {                                                \
            t_fail++;                                           \
            printf("not ok %d - %s\n", t_count, (msg));         \
        }                                                       \
        assert(ok_);                                            \
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
    CHECK(st.retries == 2,      "exactly two retries (first CFG + first EXEC/WR NACKed)");
    CHECK(st.transactions > 0,  "transaction counter advanced");

    cgra_reset_stats(dev);
    cgra_get_stats(dev, &st);
    CHECK(st.transactions == 0 && st.retries == 0, "reset_stats zeroes counters");
    cgra_close(dev);
}

/* ---- protocol v3: the fused execute transaction -------------------------- */

static void test_exec(cgra_t *dev)
{
    CHECK(cgra_has_exec(dev) == 1, "sim: advertises protocol v3 (exec)");

    /* PE(0,0) = ADD(north, west): one fused write + run + masked read-back. */
    uint32_t cfg[CGRA_NUM_PE];
    for (int i = 0; i < CGRA_NUM_PE; i++)
        cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);
    cfg[0] = CGRA_CFG(CGRA_OP_ADD, CGRA_SEL_N, CGRA_SEL_W, 0);
    CHECK(cgra_configure(dev, cfg) == CGRA_OK, "exec: configure");

    int16_t west[CGRA_ROWS] = {7, 0, 0, 0};
    int16_t north[CGRA_COLS] = {5, 0, 0, 0};
    int16_t tap = 0;
    CHECK(cgra_exec(dev, 1, CGRA_EXEC_RESET, 0x0001u, west, north, &tap, 1) == CGRA_OK,
          "exec: single-tap transaction");
    CHECK(tap == 12, "exec: tapped register holds 7 + 5");

    /* A tap mask picks exactly the requested registers, in ascending order. */
    int16_t two[2] = {0, 0};
    CHECK(cgra_exec(dev, 1, CGRA_EXEC_RESET, 0x0003u, west, north, two, 2) == CGRA_OK,
          "exec: two-tap mask");
    CHECK(two[0] == 12 && two[1] == 0, "exec: taps come back in PE order");

    /* An empty mask is a legal pure fused write+run. */
    CHECK(cgra_exec(dev, 1, CGRA_EXEC_RESET, 0x0000u, west, north, NULL, 0) == CGRA_OK,
          "exec: empty mask (write+run only)");
    int16_t regs[CGRA_NUM_PE];
    CHECK(cgra_read_regs(dev, regs) == CGRA_OK && regs[0] == 12,
          "exec: empty mask still stepped the array");

    /* ntaps must agree with the mask's population count. */
    CHECK(cgra_exec(dev, 1, 0u, 0x0003u, west, north, two, 1) == CGRA_ERR_ARG,
          "exec: rejects ntaps != popcount(mask)");

    /* The fused reset really clears the datapath: accumulate, then reset. */
    cfg[0] = CGRA_CFG(CGRA_OP_ACC, CGRA_SEL_N, CGRA_SEL_ZERO, 0);
    CHECK(cgra_configure(dev, cfg) == CGRA_OK, "exec: configure ACC");
    CHECK(cgra_exec(dev, 3, CGRA_EXEC_RESET, 0x0001u, NULL, north, &tap, 1) == CGRA_OK,
          "exec: reset then accumulate");
    CHECK(tap == 15, "exec: three accumulates of 5 from a cleared register");
    CHECK(cgra_exec(dev, 2, 0u, 0x0001u, NULL, north, &tap, 1) == CGRA_OK,
          "exec: accumulate without reset");
    CHECK(tap == 25, "exec: without the flag the accumulator carries over");
    CHECK(cgra_exec(dev, 1, CGRA_EXEC_RESET, 0x0001u, NULL, north, &tap, 1) == CGRA_OK,
          "exec: accumulate with fused reset");
    CHECK(tap == 5, "exec: fused reset cleared the accumulator first");
}

/* A device that reports protocol v2 must be detected as such, and the kernels
 * must fall back to write/run/read and produce identical results. */
static void test_legacy_fallback(void)
{
    cgra_t *dev = cgra_open("sim:v2", 115200);
    CHECK(dev != NULL, "open sim:v2 (legacy device)");
    if (!dev) return;

    cgra_info_t info;
    CHECK(cgra_identify(dev, &info) == CGRA_OK && info.version == 2,
          "sim:v2 identifies as protocol v2");
    CHECK(cgra_has_exec(dev) == 0, "sim:v2 does not advertise exec");
    CHECK(cgra_exec(dev, 1, 0u, 0u, NULL, NULL, NULL, 0) == CGRA_ERR_PROTO,
          "cgra_exec refuses a pre-v3 device");

    int16_t a[8], b[8], out[8];
    for (int i = 0; i < 8; i++) { a[i] = (int16_t)(i - 3); b[i] = (int16_t)(2 * i); }
    CHECK(cgra_vec_add(dev, a, b, out, 8) == CGRA_OK, "v2 fallback: vec_add runs");
    int ok = 1;
    for (int i = 0; i < 8; i++)
        if (out[i] != (int16_t)(a[i] + b[i])) ok = 0;
    CHECK(ok, "v2 fallback: results identical to the fused path");

    int16_t dot = 0, ref = 0;
    for (int i = 0; i < 8; i++) ref = (int16_t)(ref + (int16_t)(a[i] * b[i]));
    CHECK(cgra_dot(dev, a, b, 8, &dot) == CGRA_OK && dot == ref,
          "v2 fallback: dot identical to the fused path");
    cgra_close(dev);
}

/* The fused path must cost strictly fewer round trips than the v2 path for
 * the same element-wise work -- this is the whole point of the transaction. */
static void test_exec_saves_round_trips(void)
{
    int16_t a[64], b[64], out[64];
    for (int i = 0; i < 64; i++) { a[i] = (int16_t)i; b[i] = (int16_t)(64 - i); }

    cgra_stats_t v3, v2;
    cgra_t *d3 = cgra_open("sim:", 115200);
    cgra_t *d2 = cgra_open("sim:v2", 115200);
    CHECK(d3 != NULL && d2 != NULL, "open both protocol backends");
    if (!d3 || !d2) { if (d3) cgra_close(d3); if (d2) cgra_close(d2); return; }

    cgra_vec_add(d3, a, b, out, 64);           /* warm the cached ID handshake */
    cgra_vec_add(d2, a, b, out, 64);
    cgra_reset_stats(d3); cgra_reset_stats(d2);
    cgra_vec_add(d3, a, b, out, 64);
    cgra_vec_add(d2, a, b, out, 64);
    cgra_get_stats(d3, &v3);
    cgra_get_stats(d2, &v2);

    CHECK(v3.transactions * 2 < v2.transactions,
          "fused path uses less than half the round trips");
    CHECK(v3.tx_bytes + v3.rx_bytes < v2.tx_bytes + v2.rx_bytes,
          "fused path also moves fewer bytes");
    cgra_close(d3);
    cgra_close(d2);
}

static void test_geometry_api(void)
{
    const unsigned shapes[][2] = {{1,1}, {1,16}, {16,1}, {2,3}, {3,2}, {2,8}, {8,2}, {4,4}};
    for (size_t shape = 0; shape < sizeof(shapes) / sizeof(shapes[0]); shape++) {
        unsigned rows = shapes[shape][0], cols = shapes[shape][1];
        size_t count = rows * cols;
        char path[32];
        snprintf(path, sizeof(path), "sim:%ux%u", rows, cols);
        cgra_t *dev = cgra_open(path, 115200);
        CHECK(dev != NULL, path);
        cgra_info_t info;
        CHECK(cgra_get_info(dev, &info) == CGRA_OK && info.rows == rows && info.cols == cols,
              "ID reports actual geometry");
        uint32_t cfg[CGRA_MAX_PE];
        int16_t west[CGRA_MAX_EDGE] = {0}, north[CGRA_MAX_EDGE] = {0};
        int16_t regs[CGRA_MAX_PE + 1];
        for (size_t j = 0; j <= CGRA_MAX_PE; j++) regs[j] = 23456;
        for (size_t j = 0; j < count; j++) cfg[j] = CGRA_CFG(CGRA_OP_CONST, CGRA_SEL_ZERO, CGRA_SEL_ZERO, j + 1);
        cgra_reset_stats(dev);
        CHECK(cgra_get_info(dev, &info) == CGRA_OK, "cached ID succeeds");
        CHECK(cgra_configure_n(dev, cfg, count - 1) == CGRA_ERR_ARG, "short configuration rejected");
        CHECK(cgra_configure_n(dev, cfg, count + 1) == CGRA_ERR_ARG, "long configuration rejected");
        CHECK(cgra_write_inputs_n(dev, west, rows - 1, north, cols) == CGRA_ERR_ARG,
              "short west edge rejected");
        CHECK(cgra_write_inputs_n(dev, west, rows, north, cols + 1) == CGRA_ERR_ARG,
              "long north edge rejected");
        CHECK(cgra_read_regs_n(dev, regs, count - 1) == CGRA_ERR_ARG, "short register buffer rejected");
        CHECK(cgra_exec_n(dev, 1, 2, 0, NULL, 0, NULL, 0, NULL, 0) == CGRA_ERR_ARG,
              "unknown EXEC flags rejected");
        CHECK(cgra_exec_n(dev, 1, 0, 0, NULL, rows, NULL, 0, NULL, 0) == CGRA_ERR_ARG,
              "NULL input with nonzero length rejected");
        CHECK(cgra_exec_n(dev, 1, 0, 1, NULL, 0, NULL, 0, regs, 0) == CGRA_ERR_ARG,
              "tap count mismatch rejected");
        if (count < 16)
            CHECK(cgra_exec_n(dev, 1, 0, (uint16_t)(1u << count), NULL, 0, NULL, 0, regs, 1) == CGRA_ERR_ARG,
                  "mask selecting absent PE rejected");
        if (rows != CGRA_ROWS || cols != CGRA_COLS) {
            CHECK(cgra_configure(dev, cfg) == CGRA_ERR_GEOMETRY, "legacy CFG requires 4x4");
            CHECK(cgra_write_inputs(dev, west, north) == CGRA_ERR_GEOMETRY, "legacy WR requires 4x4");
            CHECK(cgra_read_regs(dev, regs) == CGRA_ERR_GEOMETRY, "legacy RD requires 4x4");
            CHECK(cgra_exec(dev, 1, 0, 0, NULL, NULL, NULL, 0) == CGRA_ERR_GEOMETRY,
                  "legacy EXEC requires 4x4");
            CHECK(cgra_apply(dev, cfg, west, north, 1, regs) == CGRA_ERR_GEOMETRY,
                  "legacy apply requires 4x4");
        }
        cgra_stats_t stats;
        cgra_get_stats(dev, &stats);
        CHECK(stats.transactions == 0 && stats.tx_bytes == 0 && stats.rx_bytes == 0,
              "invalid buffers/geometry perform no transfers after cached ID");
        CHECK(cgra_configure_n(dev, cfg, count) == CGRA_OK, "runtime CFG succeeds");
        CHECK(cgra_write_inputs_n(dev, west, rows, north, cols) == CGRA_OK, "runtime WR succeeds");
        CHECK(cgra_run(dev, 1) == CGRA_OK, "runtime RUN succeeds");
        CHECK(cgra_read_regs_n(dev, regs, count) == CGRA_OK, "runtime RD succeeds with exact capacity");
        int ok = regs[count] == 23456;
        for (size_t j = 0; j < count; j++) if (regs[j] != (int16_t)(j + 1)) ok = 0;
        CHECK(ok, "row-major results and read buffer canary intact");
        uint16_t mask = (uint16_t)(1u << (count - 1));
        regs[1] = 23456;
        CHECK(cgra_exec_n(dev, 1, CGRA_EXEC_RESET, mask, NULL, 0, NULL, 0, regs, 1) == CGRA_OK &&
              regs[0] == (int16_t)count && regs[1] == 23456, "last PE masked read and zero inputs");
        cgra_close(dev);
    }
    const char *invalid[] = {"sim:0x3", "sim:3x0", "sim:5x4", "sim:17x1", "sim:2x3junk", "sim:unknown"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        CHECK(cgra_open(invalid[i], 115200) == NULL, "invalid emulator geometry rejected");
}

static void test_kernel_op_validation(void)
{
    const char *backends[] = {"sim:", "sim:v2"};
    const int invalid[] = {-1, 18, CGRA_OP_NOP, CGRA_OP_PASS, CGRA_OP_MAC,
                           CGRA_OP_ACC, CGRA_OP_CONST, CGRA_OP_ABS};
    const int16_t a[] = {1, 2}, b[] = {3, 4};
    for (size_t j = 0; j < sizeof(backends) / sizeof(backends[0]); j++) {
        cgra_t *dev = cgra_open(backends[j], 115200);
        CHECK(dev != NULL, "open kernel validation device");
        if (!dev) continue;
        int16_t out[] = {-9, -9};
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            CHECK(cgra_vec_binop(dev, (enum cgra_op)invalid[i], a, b, out, 2) == CGRA_ERR_ARG,
                  "unsupported binary kernel op rejected");
            CHECK(cgra_vec_binop_imm(dev, (enum cgra_op)invalid[i], a, 1, out, 2) == CGRA_ERR_ARG,
                  "unsupported immediate kernel op rejected");
        }
        cgra_stats_t stats;
        cgra_get_stats(dev, &stats);
        CHECK(stats.transactions == 0 && out[0] == -9 && out[1] == -9,
              "invalid op does not contact device or modify output");
        cgra_close(dev);
    }
}

int main(void)
{
    test_kernel_op_validation();
    cgra_t *dev = cgra_open("sim:", 115200);
    if (!dev) { fprintf(stderr, "Bail out! cannot open sim: emulator\n"); return 1; }

    printf("# libcgra unit tests\n");
    test_cfg_word();
    test_identify(dev);
    test_primitives(dev);
    test_kernels(dev);
    test_shift(dev);
    test_dot(dev);
    test_exec(dev);
    test_errors(dev);
    cgra_close(dev);
    test_retries();
    test_legacy_fallback();
    test_exec_saves_round_trips();
    test_geometry_api();

    printf("1..%d\n", t_count);
    printf("# %d passed, %d failed\n", t_count - t_fail, t_fail);
    return t_fail ? 1 : 0;
}
