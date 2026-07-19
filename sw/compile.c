/*
 * compile.c — mode -> config words -> execution over the CGRA (or emulator).
 */

#include "compile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------- token tables */

static int op_code(const char *name)
{
    static const struct { const char *n; int c; } t[] = {
        {"nop", CGRA_OP_NOP},   {"pass", CGRA_OP_PASS}, {"add", CGRA_OP_ADD},
        {"sub", CGRA_OP_SUB},   {"mul", CGRA_OP_MUL},   {"mac", CGRA_OP_MAC},
        {"and", CGRA_OP_AND},   {"or", CGRA_OP_OR},     {"xor", CGRA_OP_XOR},
        {"shl", CGRA_OP_SHL},   {"shr", CGRA_OP_SHR},   {"max", CGRA_OP_MAX},
        {"min", CGRA_OP_MIN},   {"abs", CGRA_OP_ABS},   {"acc", CGRA_OP_ACC},
        {"const", CGRA_OP_CONST},
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (strcmp(name, t[i].n) == 0)
            return t[i].c;
    return -1;
}

static const char *op_name(int code)
{
    static const char *n[16] = {
        "nop", "pass", "add", "sub", "mul", "mac", "and", "or",
        "xor", "shl", "shr", "max", "min", "abs", "acc", "const"
    };
    return (code >= 0 && code < 16) ? n[code] : "?";
}

static const char *sel_name(int code)
{
    static const char *n[8] = {
        "north", "south", "east", "west", "const", "self", "zero", "?"
    };
    return n[code & 7];
}

void cfg_decode(uint32_t word, char *buf, size_t bufsz)
{
    int16_t imm = (int16_t)(word & 0xFFFF);
    int op   = (word >> 16) & 0xF;
    int sela = (word >> 20) & 0x7;
    int selb = (word >> 23) & 0x7;
    snprintf(buf, bufsz, "%-5s a=%-5s b=%-5s imm=%d",
             op_name(op), sel_name(sela), sel_name(selb), imm);
}

/* Parse an operand source: north|south|east|west|self|zero|const(K).
 * Returns the select code and, for const, the immediate in *imm and
 * *is_const = 1. Returns -1 on a bad source. */
static int operand_sel(const char *src, long *imm, int *is_const)
{
    *is_const = 0;
    if (strncmp(src, "const(", 6) == 0) {
        *imm = strtol(src + 6, NULL, 0);
        *is_const = 1;
        return CGRA_SEL_CONST;
    }
    if (strcmp(src, "const") == 0) { *is_const = 1; return CGRA_SEL_CONST; }
    if (strcmp(src, "north") == 0) return CGRA_SEL_N;
    if (strcmp(src, "south") == 0) return CGRA_SEL_S;
    if (strcmp(src, "east") == 0)  return CGRA_SEL_E;
    if (strcmp(src, "west") == 0)  return CGRA_SEL_W;
    if (strcmp(src, "self") == 0)  return CGRA_SEL_SELF;
    if (strcmp(src, "zero") == 0 || src[0] == '\0') return CGRA_SEL_ZERO;
    return -1;
}

/* -------------------------------------------------- execution plan */

typedef struct {
    uint32_t cfg[CGRA_NUM_PE];
    int steps;
    int accum;              /* reduction across all elements into accum_tap */
    int lanes;              /* element-wise outputs per transaction */
    int tap[CGRA_ROWS];     /* register index for each lane */
    int accum_tap;
    int a_north;            /* inject a[] on the north edge */
    int b_west;             /* inject b[] on the west edge */
    int systolic;           /* matrix-driven; run via matvec_run, not mode_run */
    int reset_each;         /* clear PE registers before every chunk */
} plan_t;

static int resolve_steps(const dsl_mode *m, int dflt)
{
    if (m->steps[0] == '\0' || strcmp(m->steps, "auto") == 0)
        return dflt;
    if (strcmp(m->steps, "rows") == 0)
        return CGRA_ROWS;
    int v = atoi(m->steps);
    return v > 0 ? v : dflt;
}

static int build_plan(const dsl_mode *m, long imm_ovr, int has_ovr,
                      plan_t *p, char *err, size_t errsz)
{
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < CGRA_NUM_PE; i++)
        p->cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);

    p->reset_each = (strcmp(m->reset, "each") == 0);
    const char *pat = m->pattern[0] ? m->pattern : "diagonal";

    if (strcmp(pat, "diagonal") == 0 || strcmp(pat, "custom") == 0) {
        p->lanes = CGRA_ROWS;
        for (int k = 0; k < CGRA_ROWS; k++)
            p->tap[k] = k * CGRA_COLS + k;    /* out = diag */
        p->steps = resolve_steps(m, CGRA_ROWS);
        p->a_north = 1;
        p->b_west = 1;

        if (strcmp(pat, "diagonal") == 0) {
            int op = op_code(m->op[0] ? m->op : "add");
            if (op < 0) { snprintf(err, errsz, "mode %s: unknown op '%s'", m->name, m->op); return -1; }

            const char *asrc = m->a[0] ? m->a : "north";
            const char *bsrc = m->b[0] ? m->b : "west";
            long ia = 0, ib = 0; int ca = 0, cb = 0;
            int sa = operand_sel(asrc, &ia, &ca);
            int sb = operand_sel(bsrc, &ib, &cb);
            if (sa < 0 || sb < 0) { snprintf(err, errsz, "mode %s: bad operand source", m->name); return -1; }

            long imm = cb ? ib : (ca ? ia : 0);
            if (has_ovr && (ca || cb)) imm = imm_ovr;      /* --imm sets the const operand */

            p->a_north = (sa == CGRA_SEL_N);
            p->b_west  = (sb == CGRA_SEL_W);

            for (int r = 0; r < CGRA_ROWS; r++)
                for (int c = 0; c < CGRA_COLS; c++) {
                    int idx = r * CGRA_COLS + c;
                    if (r == c)
                        p->cfg[idx] = CGRA_CFG(op, sa, sb, imm);
                    else if (r < c)   /* route a downward */
                        p->cfg[idx] = CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_N, CGRA_SEL_ZERO, 0);
                    else              /* route b rightward */
                        p->cfg[idx] = CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_W, CGRA_SEL_ZERO, 0);
                }
        } else { /* custom */
            for (int i = 0; i < m->npe; i++) {
                const dsl_pe *pe = &m->pe[i];
                int op = op_code(pe->op[0] ? pe->op : "nop");
                if (op < 0) { snprintf(err, errsz, "mode %s: PE %d,%d unknown op '%s'",
                                       m->name, pe->r, pe->c, pe->op); return -1; }
                long ia = 0, ib = 0; int ca = 0, cb = 0;
                int sa = operand_sel(pe->a[0] ? pe->a : "zero", &ia, &ca);
                int sb = operand_sel(pe->b[0] ? pe->b : "zero", &ib, &cb);
                if (sa < 0 || sb < 0) { snprintf(err, errsz, "mode %s: PE %d,%d bad operand",
                                                 m->name, pe->r, pe->c); return -1; }
                long imm = cb ? ib : (ca ? ia : 0);
                p->cfg[pe->r * CGRA_COLS + pe->c] = CGRA_CFG(op, sa, sb, imm);
            }
            /* out tap: "pe R,C" overrides the diagonal default */
            if (strncmp(m->out, "pe", 2) == 0) {
                int r = -1, c = -1;
                if (sscanf(m->out + 2, " %d , %d", &r, &c) == 2 &&
                    r >= 0 && r < CGRA_ROWS && c >= 0 && c < CGRA_COLS) {
                    p->lanes = 1;
                    p->tap[0] = r * CGRA_COLS + c;
                }
            }
        }
        return 0;
    }

    if (strcmp(pat, "reduce") == 0) {
        int op = op_code(m->op[0] ? m->op : "mac");
        if (op < 0) { snprintf(err, errsz, "mode %s: unknown op '%s'", m->name, m->op); return -1; }
        const char *asrc = m->a[0] ? m->a : "north";
        const char *bsrc = m->b[0] ? m->b : "west";
        long ia = 0, ib = 0; int ca = 0, cb = 0;
        int sa = operand_sel(asrc, &ia, &ca);
        int sb = operand_sel(bsrc, &ib, &cb);
        if (sa < 0 || sb < 0) { snprintf(err, errsz, "mode %s: bad operand source", m->name); return -1; }
        long imm = cb ? ib : (ca ? ia : 0);
        if (has_ovr && (ca || cb)) imm = imm_ovr;

        p->accum = 1;
        p->accum_tap = 0;
        p->steps = resolve_steps(m, 1);
        p->a_north = (sa == CGRA_SEL_N);
        p->b_west  = (sb == CGRA_SEL_W);
        p->cfg[0] = CGRA_CFG(op, sa, sb, imm);
        return 0;
    }

    if (strcmp(pat, "systolic") == 0 || strcmp(pat, "conv") == 0) {
        /* Structural weight-stationary config: row 0 multiplies the streamed
         * matrix element by the (per-column) stationary weight, rows 1..3 form
         * a delay line shifting the products south. The weights are filled in
         * per tile by matvec_run; here they are placeholders. Convolution is a
         * matrix-vector with a Toeplitz matrix, so it shares this structure. */
        for (int r = 0; r < CGRA_ROWS; r++)
            for (int c = 0; c < CGRA_COLS; c++)
                p->cfg[r * CGRA_COLS + c] = (r == 0)
                    ? CGRA_CFG(CGRA_OP_MUL, CGRA_SEL_N, CGRA_SEL_CONST, 0)
                    : CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_N, CGRA_SEL_ZERO, 0);
        p->systolic = 1;
        p->steps = CGRA_ROWS;
        return 0;
    }

    if (strcmp(pat, "scan") == 0) {
        /* Prefix scan on row 0: PE(0,c) = ADD(west, const(a[c])). The vector
         * element stays in the immediate and is re-added each step while the
         * growing prefix flows east; scan_run fills the immediates per block. */
        for (int c = 0; c < CGRA_COLS; c++)
            p->cfg[c] = CGRA_CFG(CGRA_OP_ADD, CGRA_SEL_W, CGRA_SEL_CONST, 0);
        p->systolic = 1;
        p->steps = CGRA_COLS;
        return 0;
    }

    snprintf(err, errsz, "mode %s: unknown pattern '%s'", m->name, pat);
    return -1;
}

/* -------------------------------------------------- public API */

int mode_compile(const dsl_mode *m, long imm_ovr, int has_ovr,
                 uint32_t cfg[CGRA_NUM_PE], char *err, size_t errsz)
{
    plan_t p;
    if (build_plan(m, imm_ovr, has_ovr, &p, err, errsz) != 0)
        return -1;
    memcpy(cfg, p.cfg, sizeof(p.cfg));
    return 0;
}

int mode_run(cgra_t *dev, const dsl_mode *m,
             const int16_t *a, const int16_t *b, size_t n,
             long imm_ovr, int has_ovr,
             int16_t *out, size_t out_cap,
             char *err, size_t errsz)
{
    plan_t p;
    if (build_plan(m, imm_ovr, has_ovr, &p, err, errsz) != 0)
        return CGRA_ERR_ARG;
    if (p.systolic) {
        snprintf(err, errsz,
                 "mode %s (pattern %s) is not a plain vector op; use the matching "
                 "command (matvec / conv / scan)", m->name,
                 m->pattern[0] ? m->pattern : "diagonal");
        return CGRA_ERR_ARG;
    }

    int rc = cgra_configure(dev, p.cfg);
    if (rc != CGRA_OK) goto io_err;

    if (p.accum) {
        rc = cgra_reset_datapath(dev);
        if (rc != CGRA_OK) goto io_err;
        for (size_t i = 0; i < n; i++) {
            int16_t north[CGRA_COLS] = {0};
            int16_t west[CGRA_ROWS] = {0};
            if (p.a_north) north[0] = a[i];
            if (p.b_west && b) west[0] = b[i];
            rc = cgra_write_inputs(dev, west, north);
            if (rc != CGRA_OK) goto io_err;
            rc = cgra_run(dev, (uint8_t)p.steps);
            if (rc != CGRA_OK) goto io_err;
        }
        int16_t regs[CGRA_NUM_PE];
        rc = cgra_read_regs(dev, regs);
        if (rc != CGRA_OK) goto io_err;
        if (out_cap < 1) { snprintf(err, errsz, "output buffer too small"); return CGRA_ERR_ARG; }
        out[0] = regs[p.accum_tap];
        return 1;
    }

    /* element-wise: p.lanes elements per transaction */
    if (out_cap < n) { snprintf(err, errsz, "output buffer too small"); return CGRA_ERR_ARG; }
    for (size_t i = 0; i < n; i += (size_t)p.lanes) {
        int16_t north[CGRA_COLS] = {0};
        int16_t west[CGRA_ROWS] = {0};
        size_t chunk = n - i < (size_t)p.lanes ? n - i : (size_t)p.lanes;
        if (p.reset_each) {
            rc = cgra_reset_datapath(dev);
            if (rc != CGRA_OK) goto io_err;
        }
        for (size_t j = 0; j < chunk; j++) {
            if (p.a_north) north[j] = a[i + j];
            if (p.b_west && b) west[j] = b[i + j];
        }
        rc = cgra_write_inputs(dev, west, north);
        if (rc != CGRA_OK) goto io_err;
        rc = cgra_run(dev, (uint8_t)p.steps);
        if (rc != CGRA_OK) goto io_err;
        int16_t regs[CGRA_NUM_PE];
        rc = cgra_read_regs(dev, regs);
        if (rc != CGRA_OK) goto io_err;
        for (size_t j = 0; j < chunk; j++)
            out[i + j] = regs[p.tap[j]];
    }
    return (int)n;

io_err:
    snprintf(err, errsz, "%s", cgra_strerror(rc));
    return rc;
}

/* -------------------------------------------------- systolic matrix-vector */

int matvec_run(cgra_t *dev, const int16_t *A, int M, int N, const int16_t *x,
               int16_t *y, char *err, size_t errsz)
{
    if (!dev || !A || !x || !y || M <= 0 || N <= 0) {
        if (err) snprintf(err, errsz, "matvec: invalid arguments");
        return CGRA_ERR_ARG;
    }

    for (int i = 0; i < M; i++)
        y[i] = 0;

    int rc = CGRA_OK;

    /* Tile the product over 4x4 blocks: weights x[cj..] stay in the PE
     * immediates (reconfigure per column tile), the matrix block streams in
     * one row per step. After 4 steps PE(r,c) holds A[row_base+3-r][col]*x[col];
     * summing the skewed rows on the host gives each output element, and
     * column tiles accumulate the full dot product. */
    for (int cj = 0; cj < N; cj += CGRA_COLS) {
        int16_t xt[CGRA_COLS] = {0};
        for (int c = 0; c < CGRA_COLS && cj + c < N; c++)
            xt[c] = x[cj + c];

        uint32_t cfg[CGRA_NUM_PE];
        for (int r = 0; r < CGRA_ROWS; r++)
            for (int c = 0; c < CGRA_COLS; c++)
                cfg[r * CGRA_COLS + c] = (r == 0)
                    ? CGRA_CFG(CGRA_OP_MUL, CGRA_SEL_N, CGRA_SEL_CONST, xt[c])
                    : CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_N, CGRA_SEL_ZERO, 0);

        rc = cgra_configure(dev, cfg);
        if (rc != CGRA_OK) goto io_err;

        for (int ri = 0; ri < M; ri += CGRA_ROWS) {
            rc = cgra_reset_datapath(dev);
            if (rc != CGRA_OK) goto io_err;

            for (int k = 0; k < CGRA_ROWS; k++) {
                int16_t north[CGRA_COLS] = {0};
                int16_t west[CGRA_ROWS] = {0};
                int row = ri + k;
                if (row < M)
                    for (int c = 0; c < CGRA_COLS && cj + c < N; c++)
                        north[c] = A[row * N + cj + c];
                rc = cgra_write_inputs(dev, west, north);
                if (rc != CGRA_OK) goto io_err;
                rc = cgra_run(dev, 1);
                if (rc != CGRA_OK) goto io_err;
            }

            int16_t regs[CGRA_NUM_PE];
            rc = cgra_read_regs(dev, regs);
            if (rc != CGRA_OK) goto io_err;

            for (int i = 0; i < CGRA_ROWS && ri + i < M; i++) {
                int32_t acc = 0;
                for (int c = 0; c < CGRA_COLS; c++)
                    acc += regs[(CGRA_ROWS - 1 - i) * CGRA_COLS + c];
                y[ri + i] = (int16_t)(y[ri + i] + acc);
            }
        }
    }
    return CGRA_OK;

io_err:
    snprintf(err, errsz, "%s", cgra_strerror(rc));
    return rc;
}

int scan_run(cgra_t *dev, const int16_t *a, int n, int op, int16_t *out,
             char *err, size_t errsz)
{
    if (!dev || !a || !out || n < 0) {
        if (err) snprintf(err, errsz, "scan: invalid arguments");
        return CGRA_ERR_ARG;
    }

    /* Identity element for the carry-in of the very first block. */
    int16_t carry;
    switch (op) {
    case CGRA_OP_MUL: carry = 1;         break;
    case CGRA_OP_MAX: carry = INT16_MIN; break;
    case CGRA_OP_MIN: carry = INT16_MAX; break;
    default:          op = CGRA_OP_ADD; carry = 0; break;
    }

    int rc = CGRA_OK;

    /* Process COLS elements per block on row 0: element a[j] sits in the
     * immediate of PE(0,j), the running fold flows west->east, and the
     * carry-in (previous block's fold) is injected on west_in[0]. */
    for (int base = 0; base < n; base += CGRA_COLS) {
        int m = n - base < CGRA_COLS ? n - base : CGRA_COLS;

        uint32_t cfg[CGRA_NUM_PE];
        for (int i = 0; i < CGRA_NUM_PE; i++)
            cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);
        for (int c = 0; c < m; c++)
            cfg[c] = CGRA_CFG(op, CGRA_SEL_W, CGRA_SEL_CONST, a[base + c]);

        rc = cgra_configure(dev, cfg);
        if (rc != CGRA_OK) goto io_err;
        rc = cgra_reset_datapath(dev);
        if (rc != CGRA_OK) goto io_err;

        int16_t west[CGRA_ROWS] = {0};
        int16_t north[CGRA_COLS] = {0};
        west[0] = carry;
        rc = cgra_write_inputs(dev, west, north);
        if (rc != CGRA_OK) goto io_err;
        rc = cgra_run(dev, (uint8_t)CGRA_COLS);
        if (rc != CGRA_OK) goto io_err;

        int16_t regs[CGRA_NUM_PE];
        rc = cgra_read_regs(dev, regs);
        if (rc != CGRA_OK) goto io_err;

        for (int c = 0; c < m; c++)
            out[base + c] = regs[c];
        carry = regs[m - 1];
    }
    return n;

io_err:
    snprintf(err, errsz, "%s", cgra_strerror(rc));
    return rc;
}

int conv_run(cgra_t *dev, const int16_t *h, int k, const int16_t *x, int n,
             int16_t *out, size_t out_cap, char *err, size_t errsz)
{
    if (!dev || !h || !x || k <= 0 || n <= 0) {
        if (err) snprintf(err, errsz, "conv: invalid arguments");
        return CGRA_ERR_ARG;
    }
    int m = n + k - 1;                      /* full convolution length */
    if ((size_t)m > out_cap) {
        snprintf(err, errsz, "conv: output buffer too small (need %d)", m);
        return CGRA_ERR_ARG;
    }

    /* Build the M x N Toeplitz matrix A with A[i][j] = h[i-j] (0 elsewhere),
     * so that y = A*x is the convolution, then run it on the systolic engine. */
    int16_t *A = calloc((size_t)m * (size_t)n, sizeof(int16_t));
    if (!A) { snprintf(err, errsz, "conv: out of memory"); return CGRA_ERR_ARG; }
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) {
            int t = i - j;
            if (t >= 0 && t < k)
                A[i * n + j] = h[t];
        }

    int rc = matvec_run(dev, A, m, n, x, out, err, errsz);
    free(A);
    return rc == CGRA_OK ? m : rc;
}
