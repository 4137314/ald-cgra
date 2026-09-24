/*
 * compile.c — mode -> config words -> execution over the CGRA (or emulator).
 *
 * Every engine here drives the array in chunks, and each chunk used to cost
 * three round trips (write inputs, run, read back). Where the device speaks
 * protocol v3 they instead issue one fused cgra_exec() per chunk that reads
 * back only the registers the mapping actually taps; the v2 sequence is kept
 * as the fallback so an older bitstream still works.
 */

#include "compile.h"

#include <limits.h>
#include <errno.h>
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
        char *end;
        errno = 0;
        *imm = strtol(src + 6, &end, 0);
        if (errno == ERANGE || end == src + 6 || *end != ')' || end[1] ||
            *imm < INT16_MIN || *imm > UINT16_MAX)
            return -1;
        *is_const = 1;
        return CGRA_SEL_CONST;
    }
    if (strcmp(src, "const") == 0) { *imm = 0; *is_const = 1; return CGRA_SEL_CONST; }
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
    uint32_t cfg[CGRA_MAX_PE];
    int steps;
    int accum;              /* reduction across all elements into accum_tap */
    int lanes;              /* element-wise outputs per transaction */
    int tap[CGRA_MAX_EDGE];     /* register index for each lane */
    int accum_tap;
    int a_north;            /* inject a[] on the north edge */
    int b_west;             /* inject b[] on the west edge */
    int needs_b;            /* a live west-edge source requires b[] */
    int systolic;           /* matrix-driven; run via matvec_run, not mode_run */
    int reset_each;         /* clear PE registers before every chunk */
    int stateless;          /* a chunk's result does not depend on prior PE
                             * registers -- so clearing them first is free and
                             * makes the transaction safe to retransmit */
} plan_t;

/* Mask of the registers a plan taps, for the v3 fused read-back. Returns 0
 * (meaning "cannot use a masked read") unless the taps are strictly ascending,
 * since the device replies in ascending PE order. */
static uint16_t plan_tap_mask(const plan_t *p)
{
    uint16_t m = 0;
    for (int j = 0; j < p->lanes; j++) {
        if (p->tap[j] < 0 || p->tap[j] >= CGRA_MAX_PE)
            return 0;
        if (j > 0 && p->tap[j] <= p->tap[j - 1])
            return 0;
        m = (uint16_t)(m | (uint16_t)(1u << p->tap[j]));
    }
    return m;
}

static int resolve_steps(const dsl_mode *m, int dflt, cgra_info_t g)
{
    if (m->steps[0] == '\0' || strcmp(m->steps, "auto") == 0)
        return dflt;
    if (strcmp(m->steps, "rows") == 0)
        return g.rows;
    long v;
    return dsl_integer(m->steps, 0, UINT8_MAX, &v) == 0 ? (int)v : -1;
}

static int reads_source(int op, int sa, int sb, int source)
{
    if (op == CGRA_OP_NOP || op == CGRA_OP_CONST) return 0;
    if (sa == source) return 1;
    return op != CGRA_OP_PASS && op != CGRA_OP_ABS && op != CGRA_OP_ACC && sb == source;
}

int mode_requires_b(const dsl_mode *m)
{
    if (!m) return 0;
    if (strcmp(m->pattern, "custom") == 0) {
        for (int i = 0; i < m->npe; i++) {
            const dsl_pe *pe = &m->pe[i];
            if (pe->c == 0 && reads_source(op_code(pe->op[0] ? pe->op : "nop"),
                    strcmp(pe->a, "west") == 0, strcmp(pe->b, "west") == 0, 1))
                return 1;
        }
        return 0;
    }
    int op = op_code(m->op[0] ? m->op : (strcmp(m->pattern, "reduce") == 0 ? "mac" : "add"));
    return reads_source(op, strcmp(m->a[0] ? m->a : "north", "west") == 0,
                            strcmp(m->b[0] ? m->b : "west", "west") == 0, 1);
}

static int shared_immediate(int ca, long ia, int cb, long ib,
                            int has_override, long override, long *imm)
{
    if (has_override && (override < INT16_MIN || override > UINT16_MAX)) return -1;
    if (!has_override && ca && cb && (uint16_t)ia != (uint16_t)ib) return -1;
    *imm = has_override && (ca || cb) ? override : (cb ? ib : (ca ? ia : 0));
    return 0;
}

/* Fields remain merged across files. Reject values that this pattern would
 * ignore instead of claiming that a different program was compiled. */
static int validate_pattern_fields(const dsl_mode *m, int has_override,
                                   char *err, size_t errsz)
{
    const char *pat = m->pattern[0] ? m->pattern : "diagonal";
    const char *field = NULL;
    int custom = !strcmp(pat, "custom");
    int reduction = !strcmp(pat, "reduce");
    int dedicated = !strcmp(pat, "systolic") || !strcmp(pat, "conv") || !strcmp(pat, "scan");
    if (m->npe < 0 || m->npe > CGRA_MAX_PE) field = "PE count";
    else if (!custom && m->npe) field = "pe";
    else if ((custom || dedicated) && m->op[0]) field = "op";
    else if ((custom || dedicated) && m->a[0]) field = "a";
    else if ((custom || dedicated) && m->b[0]) field = "b";
    else if (dedicated && m->steps[0]) field = "steps";
    else if (dedicated && m->out[0]) field = "out";
    else if (dedicated && m->reset[0]) field = "reset";
    else if (dedicated && has_override) field = "immediate override";
    else if (reduction && m->out[0]) field = "out (reduction always reads PE 0,0)";
    else if (reduction && !strcmp(m->reset, "each")) field = "reset=each (reduction preserves its accumulator)";
    if (field) {
        snprintf(err, errsz, "mode %s: %s is not applicable to pattern %s", m->name, field, pat);
        return -1;
    }
    return 0;
}

static int build_plan(const dsl_mode *m, long imm_ovr, int has_ovr,
                      plan_t *p, cgra_info_t g, char *err, size_t errsz)
{
    if (!m || g.rows == 0 || g.cols == 0 || g.data_w != CGRA_DATA_W ||
        g.rows * g.cols > CGRA_MAX_PE) {
        snprintf(err, errsz, "invalid mode or geometry");
        return -1;
    }
    if (validate_pattern_fields(m, has_ovr, err, errsz) != 0) return -1;
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < (g.rows * g.cols); i++)
        p->cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);

    p->reset_each = (strcmp(m->reset, "each") == 0);
    if (m->reset[0] && strcmp(m->reset, "each") && strcmp(m->reset, "once")) {
        snprintf(err, errsz, "mode %s: reset must be each or once", m->name);
        return -1;
    }
    p->needs_b = mode_requires_b(m);
    if (resolve_steps(m, 1, g) < 0) {
        snprintf(err, errsz, "mode %s: steps must be auto, rows, or 0..255", m->name);
        return -1;
    }
    if (has_ovr && (imm_ovr < INT16_MIN || imm_ovr > UINT16_MAX)) {
        snprintf(err, errsz, "mode %s: immediate must fit 16 bits", m->name);
        return -1;
    }
    const char *pat = m->pattern[0] ? m->pattern : "diagonal";
    if (m->out[0] && strcmp(m->out, "diag") &&
        !(strcmp(pat, "custom") == 0 &&
          (!strncmp(m->out, "pe ", 3) || !strncmp(m->out, "pe\t", 3)))) {
        snprintf(err, errsz, "mode %s: unsupported output selector '%s'", m->name, m->out);
        return -1;
    }

    if (strcmp(pat, "diagonal") == 0 || strcmp(pat, "custom") == 0) {
        p->lanes = g.rows < g.cols ? g.rows : g.cols;
        for (int k = 0; k < p->lanes; k++)
            p->tap[k] = k * g.cols + k;    /* out = diag */
        p->steps = resolve_steps(m, p->lanes, g);
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

            long imm;
            if (shared_immediate(ca, ia, cb, ib, has_ovr, imm_ovr, &imm) != 0) {
                snprintf(err, errsz, "mode %s: a PE has one shared immediate", m->name);
                return -1;
            }
            if (sa == CGRA_SEL_S || sa == CGRA_SEL_E || sb == CGRA_SEL_S || sb == CGRA_SEL_E) {
                snprintf(err, errsz, "mode %s: south/east sources require a custom mapping", m->name);
                return -1;
            }

            p->a_north = reads_source(op, sa, sb, CGRA_SEL_N);
            p->b_west  = reads_source(op, sa, sb, CGRA_SEL_W);
            /* The PASS routing cells hold nothing across chunks, so a diagonal
             * chunk is stateless unless the op or an operand reads back the
             * PE's own register. */
            p->stateless = op != CGRA_OP_MAC && op != CGRA_OP_ACC &&
                           op != CGRA_OP_NOP &&
                           sa != CGRA_SEL_SELF && sb != CGRA_SEL_SELF;

            for (int r = 0; r < g.rows; r++)
                for (int c = 0; c < g.cols; c++) {
                    int idx = r * g.cols + c;
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
                if (pe->r < 0 || pe->r >= g.rows || pe->c < 0 || pe->c >= g.cols) {
                    snprintf(err, errsz, "mode %s: PE %d,%d outside %ux%u mesh",
                             m->name, pe->r, pe->c, g.rows, g.cols);
                    return -1;
                }
                int op = op_code(pe->op[0] ? pe->op : "nop");
                if (op < 0) { snprintf(err, errsz, "mode %s: PE %d,%d unknown op '%s'",
                                       m->name, pe->r, pe->c, pe->op); return -1; }
                long ia = 0, ib = 0; int ca = 0, cb = 0;
                int sa = operand_sel(pe->a[0] ? pe->a : "zero", &ia, &ca);
                int sb = operand_sel(pe->b[0] ? pe->b : "zero", &ib, &cb);
                if (sa < 0 || sb < 0) { snprintf(err, errsz, "mode %s: PE %d,%d bad operand",
                                                 m->name, pe->r, pe->c); return -1; }
                long imm;
                if (shared_immediate(ca, ia, cb, ib, has_ovr, imm_ovr, &imm) != 0) {
                    snprintf(err, errsz, "mode %s: PE %d,%d has one shared immediate", m->name, pe->r, pe->c);
                    return -1;
                }
                p->cfg[pe->r * g.cols + pe->c] = CGRA_CFG(op, sa, sb, imm);
            }
            /* out tap: "pe R,C" overrides the diagonal default */
            if (strncmp(m->out, "pe", 2) == 0) {
                int r = -1, c = -1;
                if ((m->out[2] == ' ' || m->out[2] == '\t') &&
                    dsl_coordinate(m->out + 3, &r, &c) == 0 && r < g.rows && c < g.cols) {
                    p->lanes = 1;
                    p->tap[0] = r * g.cols + c;
                } else {
                    snprintf(err, errsz, "mode %s: output tap outside %ux%u mesh",
                             m->name, g.rows, g.cols);
                    return -1;
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
        long imm;
        if (shared_immediate(ca, ia, cb, ib, has_ovr, imm_ovr, &imm) != 0) {
            snprintf(err, errsz, "mode %s: a PE has one shared immediate", m->name);
            return -1;
        }
        if (sa == CGRA_SEL_S || sa == CGRA_SEL_E || sb == CGRA_SEL_S || sb == CGRA_SEL_E) {
            snprintf(err, errsz, "mode %s: south/east sources require a custom mapping", m->name);
            return -1;
        }

        p->accum = 1;
        p->accum_tap = 0;
        p->steps = resolve_steps(m, 1, g);
        p->a_north = reads_source(op, sa, sb, CGRA_SEL_N);
        p->b_west  = reads_source(op, sa, sb, CGRA_SEL_W);
        p->cfg[0] = CGRA_CFG(op, sa, sb, imm);
        return 0;
    }

    if (strcmp(pat, "systolic") == 0 || strcmp(pat, "conv") == 0) {
        /* Structural weight-stationary config: row 0 multiplies the streamed
         * matrix element by the (per-column) stationary weight, the remaining rows form
         * a delay line shifting the products south. The weights are filled in
         * per tile by matvec_run; here they are placeholders. Convolution is a
         * matrix-vector with a Toeplitz matrix, so it shares this structure. */
        for (int r = 0; r < g.rows; r++)
            for (int c = 0; c < g.cols; c++)
                p->cfg[r * g.cols + c] = (r == 0)
                    ? CGRA_CFG(CGRA_OP_MUL, CGRA_SEL_N, CGRA_SEL_CONST, 0)
                    : CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_N, CGRA_SEL_ZERO, 0);
        p->systolic = 1;
        p->steps = g.rows;
        return 0;
    }

    if (strcmp(pat, "scan") == 0) {
        /* Prefix scan on row 0: PE(0,c) = op(west, north). The block's column
         * elements arrive on the north edge (one per column) and the running
         * fold flows west->east, so the configuration is independent of the
         * data and scan_run loads it ONCE for the whole vector instead of
         * re-sending 66 configuration bytes per four elements. */
        for (int c = 0; c < g.cols; c++)
            p->cfg[c] = CGRA_CFG(CGRA_OP_ADD, CGRA_SEL_W, CGRA_SEL_N, 0);
        p->systolic = 1;
        p->steps = g.cols;
        return 0;
    }

    snprintf(err, errsz, "mode %s: unknown pattern '%s'", m->name, pat);
    return -1;
}

/* -------------------------------------------------- public API */

int mode_compile_for_geometry(const dsl_mode *m, long imm_ovr, int has_ovr,
                              cgra_info_t g, uint32_t *cfg, size_t capacity,
                              char *err, size_t errsz)
{
    plan_t p;
    if (cfg == NULL || capacity < (size_t)g.rows * g.cols) {
        snprintf(err, errsz, "configuration buffer too small");
        return -1;
    }
    if (build_plan(m, imm_ovr, has_ovr, &p, g, err, errsz) != 0)
        return -1;
    memcpy(cfg, p.cfg, (size_t)g.rows * g.cols * sizeof(*cfg));
    return 0;
}

int mode_compile(const dsl_mode *m, long imm_ovr, int has_ovr,
                 uint32_t cfg[CGRA_NUM_PE], char *err, size_t errsz)
{
    const cgra_info_t g = {CGRA_PROTO_VER, CGRA_ROWS, CGRA_COLS, CGRA_DATA_W};
    return mode_compile_for_geometry(m, imm_ovr, has_ovr, g, cfg, CGRA_NUM_PE, err, errsz);
}

int pipeline_validate(dsl_ctx *ctx, const dsl_pipeline *pipeline,
                      cgra_info_t geometry, char *err, size_t errsz)
{
    if (!ctx || !pipeline || pipeline->nstage <= 0) {
        snprintf(err, errsz, "pipeline has no stages");
        return CGRA_ERR_ARG;
    }
    for (int s = 0; s < pipeline->nstage; s++) {
        const dsl_stage *stage = &pipeline->stage[s];
        dsl_mode *m = dsl_find_mode(ctx, stage->mode);
        plan_t p;
        if (!m) {
            snprintf(err, errsz, "pipeline %s: unknown stage %s", pipeline->name, stage->mode);
            return CGRA_ERR_ARG;
        }
        if (build_plan(m, stage->imm, stage->has_imm, &p, geometry, err, errsz) != 0)
            return CGRA_ERR_ARG;
        if (p.systolic || (s > 0 && p.needs_b)) {
            snprintf(err, errsz, "pipeline %s: stage %s %s", pipeline->name, stage->mode,
                     p.systolic ? "requires a dedicated command" : "requires unavailable vector B");
            return CGRA_ERR_ARG;
        }
    }
    return CGRA_OK;
}

int mode_run(cgra_t *dev, const dsl_mode *m,
             const int16_t *a, const int16_t *b, size_t n,
             long imm_ovr, int has_ovr,
             int16_t *out, size_t out_cap,
             char *err, size_t errsz)
{
    if (!dev || !m || !a || !out || n > INT_MAX) return CGRA_ERR_ARG;
    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) goto io_err;
    plan_t p;
    if (build_plan(m, imm_ovr, has_ovr, &p, g, err, errsz) != 0)
        return CGRA_ERR_ARG;
    if (p.systolic) {
        snprintf(err, errsz,
                 "mode %s (pattern %s) is not a plain vector op; use the matching "
                 "command (matvec / conv / scan)", m->name,
                 m->pattern[0] ? m->pattern : "diagonal");
        return CGRA_ERR_ARG;
    }
    if (p.needs_b && !b) {
        snprintf(err, errsz, "mode %s requires vector B on the west edge", m->name);
        return CGRA_ERR_ARG;
    }
    if (out_cap < (p.accum ? 1 : n)) {
        snprintf(err, errsz, "output buffer too small");
        return CGRA_ERR_ARG;
    }

    rc = cgra_configure_n(dev, p.cfg, (size_t)g.rows * g.cols);
    if (rc != CGRA_OK) goto io_err;

    const int exec = cgra_has_exec(dev);

    if (p.accum) {
        rc = cgra_reset_datapath(dev);
        if (rc != CGRA_OK) goto io_err;
        for (size_t i = 0; i < n; i++) {
            int16_t north[CGRA_MAX_EDGE] = {0};
            int16_t west[CGRA_MAX_EDGE] = {0};
            if (p.a_north) north[0] = a[i];
            if (p.b_west && b) west[0] = b[i];
            if (exec) {
                /* No tap and no reset: the accumulator being built up IS the
                 * state, so this is a pure fused write+run. */
                rc = cgra_exec_n(dev, (uint8_t)p.steps, 0u, 0u, west, g.rows, north, g.cols, NULL, 0);
                if (rc != CGRA_OK) goto io_err;
                continue;
            }
            rc = cgra_write_inputs_n(dev, west, g.rows, north, g.cols);
            if (rc != CGRA_OK) goto io_err;
            rc = cgra_run(dev, (uint8_t)p.steps);
            if (rc != CGRA_OK) goto io_err;
        }
        int16_t regs[CGRA_MAX_PE];
        rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
        if (rc != CGRA_OK) goto io_err;
        out[0] = regs[p.accum_tap];
        return 1;
    }

    /* element-wise: p.lanes elements per transaction */

    const uint16_t mask   = plan_tap_mask(&p);
    const int      fused  = exec && mask != 0;

    for (size_t i = 0; i < n; i += (size_t)p.lanes) {
        int16_t north[CGRA_MAX_EDGE] = {0};
        int16_t west[CGRA_MAX_EDGE] = {0};
        size_t chunk = n - i < (size_t)p.lanes ? n - i : (size_t)p.lanes;
        for (size_t j = 0; j < chunk; j++) {
            if (p.a_north) north[j] = a[i + j];
            if (p.b_west && b) west[j] = b[i + j];
        }

        if (fused) {
            /* First chunk always starts this invocation from zero. A fully
             * settled stateless diagonal may also reset later chunks for
             * retry eligibility. Underscheduled mappings must retain state
             * exactly as reset=once specifies, on both protocol versions. */
            unsigned eflags = (p.reset_each || i == 0 ||
                               (p.stateless && p.steps >= p.lanes)) ? CGRA_EXEC_RESET : 0u;
            /* One round trip per chunk, reading back only the tapped lanes. */
            int16_t taps[CGRA_MAX_EDGE];
            rc = cgra_exec_n(dev, (uint8_t)p.steps, eflags, mask,
                           west, g.rows, north, g.cols, taps, (size_t)p.lanes);
            if (rc != CGRA_OK) goto io_err;
            for (size_t j = 0; j < chunk; j++)
                out[i + j] = taps[j];
            continue;
        }

        if (p.reset_each || i == 0) {
            rc = cgra_reset_datapath(dev);
            if (rc != CGRA_OK) goto io_err;
        }
        rc = cgra_write_inputs_n(dev, west, g.rows, north, g.cols);
        if (rc != CGRA_OK) goto io_err;
        rc = cgra_run(dev, (uint8_t)p.steps);
        if (rc != CGRA_OK) goto io_err;
        int16_t regs[CGRA_MAX_PE];
        rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
        if (rc != CGRA_OK) goto io_err;
        for (size_t j = 0; j < chunk; j++)
            out[i + j] = regs[p.tap[j]];
    }
    return (int)n;

io_err:
    snprintf(err, errsz, "%s", cgra_strerror(rc));
    return rc;
}

/* CLI adapters preserve their historical return lengths and error buffers. */
static int kernel_status(int rc, char *err, size_t errsz)
{
    if (rc != CGRA_OK && err && errsz) snprintf(err, errsz, "%s", cgra_strerror(rc));
    return rc;
}

int matvec_run(cgra_t *dev, const int16_t *A, int M, int N, const int16_t *x,
               int16_t *y, char *err, size_t errsz)
{
    int rc = M <= 0 || N <= 0 ? CGRA_ERR_ARG
        : cgra_matvec(dev, A, (size_t)M, (size_t)N, x, y, (size_t)M);
    return kernel_status(rc, err, errsz);
}

int scan_run(cgra_t *dev, const int16_t *a, int n, int op, int16_t *out,
             char *err, size_t errsz)
{
    int rc = n < 0 ? CGRA_ERR_ARG
        : cgra_scan(dev, (enum cgra_op)op, a, (size_t)n, out, (size_t)n);
    return kernel_status(rc, err, errsz) == CGRA_OK ? n : rc;
}

int conv_run(cgra_t *dev, const int16_t *h, int k, const int16_t *x, int n,
             int16_t *out, size_t out_cap, char *err, size_t errsz)
{
    if (k <= 0 || n <= 0 || k - 1 > INT_MAX - n)
        return kernel_status(CGRA_ERR_ARG, err, errsz);
    int rc = cgra_conv(dev, h, (size_t)k, x, (size_t)n, out, out_cap);
    return kernel_status(rc, err, errsz) == CGRA_OK ? n + (k - 1) : rc;
}
