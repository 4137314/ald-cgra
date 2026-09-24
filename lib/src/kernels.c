/* Geometry-aware matrix, scan and convolution engines. No CLI/DSL dependency. */
#include "cgra.h"
#include "buffers.h"
#include <stdlib.h>
#include <string.h>

int cgra_matvec(cgra_t *dev, const int16_t *A, size_t rows, size_t cols,
                const int16_t *x, int16_t *y, size_t y_capacity)
{
    if (!dev) return CGRA_ERR_ARG;
    if (!rows) return CGRA_OK;
    if (!cgra_buffer_valid(y, rows) || y_capacity < rows) return CGRA_ERR_ARG;
    if (!cols) { memset(y, 0, rows * sizeof(*y)); return CGRA_OK; }
    if (cols > CGRA_MAX_ELEMENTS || rows > CGRA_MAX_ELEMENTS / cols ||
        !cgra_buffer_valid(A, rows * cols) || !cgra_buffer_valid(x, cols) ||
        cgra_buffers_overlap(y, rows, A, rows * cols) ||
        cgra_buffers_overlap(y, rows, x, cols)) return CGRA_ERR_ARG;

    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;

    memset(y, 0, rows * sizeof(*y));

    /* Tile the product over rows x cols blocks: weights x[cj..] stay in the PE
     * immediates (reconfigure per column tile), the matrix block streams in
     * one row per step. After R steps PE(r,c) holds A[row_base+R-1-r][col]*x[col];
     * summing the skewed rows on the host gives each output element, and
     * column tiles accumulate the full dot product. */
    const int exec = g.version >= 3;

    for (size_t cj = 0; cj < cols; cj += g.cols) {
        int16_t xt[CGRA_MAX_EDGE] = {0};
        int     x_nz = 0;
        for (size_t c = 0; c < g.cols && cj + c < cols; c++) {
            xt[c] = x[cj + c];
            if (xt[c] != 0) x_nz = 1;
        }
        /* Every product in this column tile has a zero weight, so the whole
         * tile contributes nothing: do not configure it and do not stream it. */
        if (!x_nz)
            continue;

        uint32_t cfg[CGRA_MAX_PE];
        for (int r = 0; r < g.rows; r++)
            for (int c = 0; c < g.cols; c++)
                cfg[r * g.cols + c] = (r == 0)
                    ? CGRA_CFG(CGRA_OP_MUL, CGRA_SEL_N, CGRA_SEL_CONST, xt[c])
                    : CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_N, CGRA_SEL_ZERO, 0);

        /* Loaded lazily, so a column tile whose every block turns out to be
         * zero costs not even a configuration. */
        int cfg_loaded = 0;

        for (size_t ri = 0; ri < rows; ri += g.rows) {
            int16_t regs[CGRA_MAX_PE];

            /* An all-zero rows x cols block of A adds zero to y, whatever x is, so the
             * rows wavefront steps that would stream it are pure wire cost.
             * Dense matrices pay one rows*cols-element scan per tile for this; banded
             * ones -- notably the Toeplitz matrix cgra_conv builds, which is
             * nearly all zeros -- save most of the transfer. */
            int a_nz = 0;
            for (size_t k = 0; k < g.rows && ri + k < rows && !a_nz; k++)
                for (size_t c = 0; c < g.cols && cj + c < cols; c++)
                    if (A[(ri + k) * cols + cj + c] != 0) { a_nz = 1; break; }
            if (!a_nz)
                continue;

            if (!cfg_loaded) {
                rc = cgra_configure_n(dev, cfg, (size_t)g.rows * g.cols);
                if (rc != CGRA_OK) return rc;
                cfg_loaded = 1;
            }

            if (!exec) {
                rc = cgra_reset_datapath(dev);
                if (rc != CGRA_OK) return rc;
            }

            for (size_t k = 0; k < g.rows; k++) {
                int16_t north[CGRA_MAX_EDGE] = {0};
                int16_t west[CGRA_MAX_EDGE] = {0};
                size_t row = ri + k;
                if (row < rows)
                    for (size_t c = 0; c < g.cols && cj + c < cols; c++)
                        north[c] = A[row * cols + cj + c];
                if (exec) {
                    /* The first wavefront step folds in the datapath clear that
                     * flushes the previous tile's delay line; the last one also
                     * reads the whole array back, so an R-step tile costs R
                     * round trips instead of 2*R+2. */
                    int last = (k + 1 == g.rows);
                    rc = cgra_exec_n(dev, 1, k == 0 ? CGRA_EXEC_RESET : 0u,
                                   (uint16_t)(last ? (1u << (g.rows * g.cols)) - 1u : 0u),
                                   west, g.rows, north, g.cols,
                                   last ? regs : NULL, last ? (g.rows * g.cols) : 0);
                    if (rc != CGRA_OK) return rc;
                    continue;
                }
                rc = cgra_write_inputs_n(dev, west, g.rows, north, g.cols);
                if (rc != CGRA_OK) return rc;
                rc = cgra_run(dev, 1);
                if (rc != CGRA_OK) return rc;
            }

            if (!exec) {
                rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
                if (rc != CGRA_OK) return rc;
            }

            for (size_t i = 0; i < g.rows && ri + i < rows; i++) {
                int32_t acc = 0;
                for (size_t c = 0; c < g.cols; c++)
                    acc += regs[((size_t)g.rows - 1 - i) * g.cols + c];
                y[ri + i] = (int16_t)(y[ri + i] + acc);
            }
        }
    }
    return CGRA_OK;
}

int cgra_scan(cgra_t *dev, enum cgra_op op, const int16_t *a, size_t n,
              int16_t *out, size_t out_capacity)
{
    if (!dev || !cgra_buffer_valid(a, n) || !cgra_buffer_valid(out, n) ||
        out_capacity < n ||
        (out != a && cgra_buffers_overlap(out, n, a, n))) return CGRA_ERR_ARG;

    /* Identity element for the carry-in of the very first block. */
    int16_t carry;
    switch (op) {
    case CGRA_OP_ADD: carry = 0;         break;
    case CGRA_OP_MUL: carry = 1;         break;
    case CGRA_OP_MAX: carry = INT16_MIN; break;
    case CGRA_OP_MIN: carry = INT16_MAX; break;
    default:
        return CGRA_ERR_ARG;
    }

    if (!n) return CGRA_OK;

    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;

    /* Row 0 holds the fold: PE(0,c) = op(west, north). The block's elements
     * arrive on the north edge and the running fold flows west->east, with the
     * previous block's result injected as the carry on west_in[0].
     *
     * Because the elements ride the north edge rather than the PE immediates,
     * the configuration is data-independent and is loaded ONCE for the whole
     * vector -- the earlier mapping re-sent a 67-byte CFG for every four
     * elements, which dominated the scan's wire cost outright.
     *
     * COLS steps are enough for the chain to flush: PE(0,c) settles at step
     * c+1, so no stale value survives and the fused reset (which makes the
     * transaction retriable) does not change the result. */
    uint32_t cfg[CGRA_MAX_PE];
    for (int i = 0; i < (g.rows * g.cols); i++)
        cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);
    for (int c = 0; c < g.cols; c++)
        cfg[c] = CGRA_CFG(op, CGRA_SEL_W, CGRA_SEL_N, 0);

    rc = cgra_configure_n(dev, cfg, (size_t)g.rows * g.cols);
    if (rc != CGRA_OK) return rc;

    const int exec = g.version >= 3;
    const uint16_t mask = (uint16_t)((1u << g.cols) - 1u);   /* row 0 */

    for (size_t base = 0; base < n; base += g.cols) {
        size_t m = n - base < g.cols ? n - base : g.cols;

        int16_t west[CGRA_MAX_EDGE] = {0};
        int16_t north[CGRA_MAX_EDGE] = {0};
        int16_t regs[CGRA_MAX_PE];
        west[0] = carry;
        for (size_t c = 0; c < m; c++)
            north[c] = a[base + c];

        if (exec) {
            rc = cgra_exec_n(dev, (uint8_t)g.cols, CGRA_EXEC_RESET, mask,
                           west, g.rows, north, g.cols, regs, g.cols);
            if (rc != CGRA_OK) return rc;
        } else {
            rc = cgra_reset_datapath(dev);
            if (rc != CGRA_OK) return rc;
            rc = cgra_write_inputs_n(dev, west, g.rows, north, g.cols);
            if (rc != CGRA_OK) return rc;
            rc = cgra_run(dev, (uint8_t)g.cols);
            if (rc != CGRA_OK) return rc;
            rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
            if (rc != CGRA_OK) return rc;
        }

        for (size_t c = 0; c < m; c++)
            out[base + c] = regs[c];
        carry = regs[m - 1];
    }
    return CGRA_OK;
}

int cgra_conv(cgra_t *dev, const int16_t *h, size_t k,
              const int16_t *x, size_t n, int16_t *out, size_t out_capacity)
{
    if (!dev) return CGRA_ERR_ARG;
    if (!k || !n) return CGRA_OK;
    if (!cgra_buffer_valid(h, k) || !cgra_buffer_valid(x, n) ||
        k - 1 > CGRA_MAX_ELEMENTS - n) return CGRA_ERR_ARG;
    size_t m = n + (k - 1);
    if (!cgra_buffer_valid(out, m) || out_capacity < m ||
        m > CGRA_MAX_ELEMENTS / n ||
        cgra_buffers_overlap(out, m, h, k) ||
        cgra_buffers_overlap(out, m, x, n)) return CGRA_ERR_ARG;

    /* Dense Toeplitz workspace; only the device engine skips zero tiles. */
    int16_t *A = calloc(m * n, sizeof(*A));
    if (!A) return CGRA_ERR_NOMEM;
    for (size_t i = 0; i < m; i++)
        for (size_t j = 0; j < n; j++)
            if (i >= j && i - j < k) A[i * n + j] = h[i - j];
    int rc = cgra_matvec(dev, A, m, n, x, out, out_capacity);
    free(A);
    return rc;
}
