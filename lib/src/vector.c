/* Diagonal vector kernels and serial dot reduction, using the public API. */
#include "cgra.h"
#include "buffers.h"

/* -------------------------------------------------- accelerated kernels */

/*
 * Diagonal element-wise configuration:
 *   - a[] enters from the north edge (one element per column),
 *   - b[] enters from the west edge (one element per row),
 *   - PEs above the diagonal PASS the a value southwards,
 *   - PEs below the diagonal PASS the b value eastwards,
 *   - the diagonal PE(k,k) computes a[k] <op> b[k].
 * After min(rows, cols) steps every diagonal register holds its result.
 */
static void diag_cfg(uint32_t cfg[CGRA_MAX_PE], enum cgra_op op,
                     int b_is_imm, int16_t imm, cgra_info_t g)
{
    for (int r = 0; r < g.rows; r++) {
        for (int c = 0; c < g.cols; c++) {
            uint32_t w;
            if (r == c)
                w = CGRA_CFG(op, CGRA_SEL_N,
                             b_is_imm ? CGRA_SEL_CONST : CGRA_SEL_W, imm);
            else if (r < c)
                w = CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_N, CGRA_SEL_ZERO, 0);
            else
                w = CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_W, CGRA_SEL_ZERO, 0);
            cfg[r * g.cols + c] = w;
        }
    }
}

/* Tap mask selecting the diagonal PEs (0, 5, 10, 15 on a 4x4 fabric):
 * exactly the registers a diagonal element-wise chunk produces. */
static uint16_t diag_mask(cgra_info_t g)
{
    uint16_t m = 0;
    int lanes = g.rows < g.cols ? g.rows : g.cols;
    for (int k = 0; k < lanes; k++)
        m = (uint16_t)(m | (1u << (k * g.cols + k)));
    return m;
}

static int diag_vec_op(cgra_t *dev, enum cgra_op op,
                       const int16_t *a, const int16_t *b, int16_t imm,
                       int16_t *out, size_t n)
{
    if (dev == NULL || !cgra_buffer_valid(a, n) || !cgra_buffer_valid(out, n) ||
        (b && !cgra_buffer_valid(b, n)) ||
        (out != a && cgra_buffers_overlap(out, n, a, n)) ||
        (b && out != b && cgra_buffers_overlap(out, n, b, n)))
        return CGRA_ERR_ARG;
    switch (op) {
    case CGRA_OP_ADD: case CGRA_OP_SUB: case CGRA_OP_MUL:
    case CGRA_OP_AND: case CGRA_OP_OR: case CGRA_OP_XOR:
    case CGRA_OP_SHL: case CGRA_OP_SHR: case CGRA_OP_MAX: case CGRA_OP_MIN:
        break;
    default:
        return CGRA_ERR_ARG;  /* these kernels require stateless binary ops */
    }
    if (!n) return CGRA_OK;

    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;
    size_t lanes = g.rows < g.cols ? g.rows : g.cols;
    uint32_t cfg[CGRA_MAX_PE] = {0};
    diag_cfg(cfg, op, b == NULL, imm, g);
    rc = cgra_configure_n(dev, cfg, (size_t)g.rows * g.cols);
    if (rc != CGRA_OK)
        return rc;

    /* One fused transaction per chunk when the device speaks v3, reading back
     * only the diagonal taps instead of all PE registers. The fused
     * reset costs nothing here (the diagonal mapping is stateless) and buys
     * idempotence, so the transaction can be retransmitted after a NACK. */
    const int exec = cgra_has_exec(dev);
    const uint16_t mask = diag_mask(g);

    for (size_t i = 0; i < n; i += lanes) {
        int16_t north[CGRA_MAX_EDGE] = { 0 };
        int16_t west[CGRA_MAX_EDGE]  = { 0 };
        int16_t regs[CGRA_MAX_PE];
        size_t m = n - i < lanes ? n - i : lanes;

        for (size_t j = 0; j < m; j++) {
            north[j] = a[i + j];
            if (b != NULL)
                west[j] = b[i + j];
        }
        if (exec) {
            int16_t taps[CGRA_MAX_PE];
            rc = cgra_exec_n(dev, (uint8_t)lanes, CGRA_EXEC_RESET, mask,
                             west, g.rows, north, g.cols, taps, lanes);
            if (rc != CGRA_OK)
                return rc;
            for (size_t j = 0; j < m; j++)
                out[i + j] = taps[j];
            continue;
        }
        rc = cgra_write_inputs_n(dev, west, g.rows, north, g.cols);
        if (rc != CGRA_OK)
            return rc;
        rc = cgra_run(dev, (uint8_t)lanes);
        if (rc != CGRA_OK)
            return rc;
        rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
        if (rc != CGRA_OK)
            return rc;
        for (size_t j = 0; j < m; j++)
            out[i + j] = regs[j * g.cols + j];
    }
    return CGRA_OK;
}

int cgra_vec_binop(cgra_t *dev, enum cgra_op op,
                   const int16_t *a, const int16_t *b, int16_t *out, size_t n)
{
    if (b == NULL && n != 0)
        return CGRA_ERR_ARG;
    return diag_vec_op(dev, op, a, b, 0, out, n);
}

int cgra_vec_binop_imm(cgra_t *dev, enum cgra_op op,
                       const int16_t *a, int16_t imm, int16_t *out, size_t n)
{
    return diag_vec_op(dev, op, a, NULL, imm, out, n);
}

int cgra_vec_add(cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n)
{
    return cgra_vec_binop(dev, CGRA_OP_ADD, a, b, out, n);
}

int cgra_vec_sub(cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n)
{
    return cgra_vec_binop(dev, CGRA_OP_SUB, a, b, out, n);
}

int cgra_vec_mul(cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n)
{
    return cgra_vec_binop(dev, CGRA_OP_MUL, a, b, out, n);
}

int cgra_vec_min(cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n)
{
    return cgra_vec_binop(dev, CGRA_OP_MIN, a, b, out, n);
}

int cgra_vec_max(cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n)
{
    return cgra_vec_binop(dev, CGRA_OP_MAX, a, b, out, n);
}

int cgra_vec_relu(cgra_t *dev, const int16_t *a, int16_t *out, size_t n)
{
    return cgra_vec_binop_imm(dev, CGRA_OP_MAX, a, 0, out, n);
}

int cgra_vec_addi(cgra_t *dev, const int16_t *a, int16_t imm, int16_t *out, size_t n)
{
    return cgra_vec_binop_imm(dev, CGRA_OP_ADD, a, imm, out, n);
}

int cgra_vec_muli(cgra_t *dev, const int16_t *a, int16_t imm, int16_t *out, size_t n)
{
    return cgra_vec_binop_imm(dev, CGRA_OP_MUL, a, imm, out, n);
}

int cgra_dot(cgra_t *dev, const int16_t *a, const int16_t *b, size_t n,
             int16_t *result)
{
    if (dev == NULL || !cgra_buffer_valid(a, n) || !cgra_buffer_valid(b, n) || result == NULL)
        return CGRA_ERR_ARG;
    if (!n) { *result = 0; return CGRA_OK; }

    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;

    /* PE(0,0) reads the north and west input ports directly: one MAC per
     * element, accumulated on the device. */
    uint32_t cfg[CGRA_MAX_PE];
    for (int i = 0; i < (g.rows * g.cols); i++)
        cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);
    cfg[0] = CGRA_CFG(CGRA_OP_MAC, CGRA_SEL_N, CGRA_SEL_W, 0);

    rc = cgra_configure_n(dev, cfg, (size_t)g.rows * g.cols);
    if (rc != CGRA_OK)
        return rc;
    rc = cgra_reset_datapath(dev);
    if (rc != CGRA_OK)
        return rc;

    /* The accumulator lives in PE(0,0)'s register, so this loop is inherently
     * one element per round trip -- the reduction mapping trades throughput for
     * on-device accumulation. v3 at least halves the round trips by fusing the
     * operand write and the step; the fused reset must stay off, since that is
     * precisely the state being accumulated. */
    const int exec = cgra_has_exec(dev);

    for (size_t i = 0; i < n; i++) {
        int16_t north[CGRA_MAX_EDGE] = { a[i], 0, 0, 0 };
        int16_t west[CGRA_MAX_EDGE]  = { b[i], 0, 0, 0 };
        if (exec) {
            rc = cgra_exec_n(dev, 1, 0u, 0u, west, g.rows, north, g.cols, NULL, 0);
            if (rc != CGRA_OK)
                return rc;
            continue;
        }
        rc = cgra_write_inputs_n(dev, west, g.rows, north, g.cols);
        if (rc != CGRA_OK)
            return rc;
        rc = cgra_run(dev, 1);
        if (rc != CGRA_OK)
            return rc;
    }

    int16_t regs[CGRA_MAX_PE];
    rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
    if (rc != CGRA_OK)
        return rc;
    *result = regs[0];
    return CGRA_OK;
}
