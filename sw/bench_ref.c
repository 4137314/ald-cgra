#include "bench_ref.h"

#include <stdio.h>
#include <string.h>

enum source { NORTH, WEST, ZERO, CONSTANT, SELF, UNSUPPORTED };

static enum source source_kind(const char *text, int16_t *constant)
{
    if (!strcmp(text, "north")) return NORTH;
    if (!strcmp(text, "west")) return WEST;
    if (!strcmp(text, "zero")) return ZERO;
    if (!strcmp(text, "self")) return SELF;
    if (!strcmp(text, "const")) { *constant = 0; return CONSTANT; }
    if (!strncmp(text, "const(", 6)) {
        char number[DSL_TOK];
        size_t len = strlen(text);
        if (len < 8 || len >= sizeof(number) || text[len - 1] != ')') return UNSUPPORTED;
        memcpy(number, text + 6, len - 7); number[len - 7] = 0;
        long value;
        if (dsl_integer(number, INT16_MIN, UINT16_MAX, &value)) return UNSUPPORTED;
        *constant = (int16_t)(value <= INT16_MAX ? value : value - 65536);
        return CONSTANT;
    }
    return UNSUPPORTED;
}

static int16_t operand(enum source kind, int16_t a, int16_t b,
                       int16_t constant, int16_t previous)
{
    switch (kind) {
    case NORTH: return a;
    case WEST: return b;
    case CONSTANT: return constant;
    case SELF: return previous;
    default: return 0;
    }
}

/* Signed arithmetic and modulo 2^16, independent of the PE implementation. */
static int16_t scalar(const char *op, int16_t a, int16_t b, int16_t state, int16_t imm)
{
    int64_t result = 0;
    if (!strcmp(op, "add")) result = (int32_t)a + b;
    else if (!strcmp(op, "sub")) result = (int32_t)a - b;
    else if (!strcmp(op, "mul")) result = (int32_t)a * b;
    else if (!strcmp(op, "mac")) result = (int64_t)a * b + state;
    else if (!strcmp(op, "acc")) result = (int32_t)a + state;
    else if (!strcmp(op, "max")) result = a > b ? a : b;
    else if (!strcmp(op, "min")) result = a < b ? a : b;
    else if (!strcmp(op, "and")) result = (uint16_t)a & (uint16_t)b;
    else if (!strcmp(op, "or")) result = (uint16_t)a | (uint16_t)b;
    else if (!strcmp(op, "xor")) result = (uint16_t)a ^ (uint16_t)b;
    else if (!strcmp(op, "shl")) result = (uint32_t)(uint16_t)a << ((uint16_t)b & 15);
    else if (!strcmp(op, "shr")) {
        /* Arithmetic shift is floor division, including negative operands. */
        int32_t divisor = 1 << ((uint16_t)b & 15);
        result = a >= 0 ? a / divisor : -((-(int32_t)a + divisor - 1) / divisor);
    }
    else if (!strcmp(op, "abs")) result = a < 0 ? -(int32_t)a : a;
    else if (!strcmp(op, "pass")) result = a;
    else if (!strcmp(op, "const")) result = imm;
    else if (!strcmp(op, "nop")) result = state;
    uint16_t bits = (uint16_t)result;
    return (int16_t)(bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536);
}

int bench_reference(const dsl_mode *m, cgra_info_t g,
                    const int16_t *a, const int16_t *b, size_t n,
                    int16_t *out, char *reason, size_t reason_size)
{
    if (!strcmp(m->pattern, "systolic")) {
        for (size_t row = 0; row < n; row++) {
            int64_t sum = 0;
            for (size_t col = 0; col < 16; col++)
                sum += (int32_t)a[row * 16 + col] * b[col];
            uint16_t bits = (uint16_t)sum;
            out[row] = (int16_t)(bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536);
        }
        return (int)n;
    }
    if (!strcmp(m->pattern, "conv")) {
        for (size_t i = 0; i < n + 4; i++) {
            int64_t sum = 0;
            for (size_t j = 0; j < 5; j++)
                if (i >= j && i - j < n) sum += (int32_t)b[j] * a[i - j];
            uint16_t bits = (uint16_t)sum;
            out[i] = (int16_t)(bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536);
        }
        return (int)(n + 4);
    }
    if (!strcmp(m->pattern, "scan")) {
        int16_t sum = 0;
        for (size_t i = 0; i < n; i++) out[i] = sum = scalar("add", sum, a[i], 0, 0);
        return (int)n;
    }
    if (!strcmp(m->pattern, "custom")) {
        /* Recognize the complete hand-placed ADD example by its structure,
         * never by its mutable name. Other custom mappings stay explicit skips. */
        unsigned seen = 0;
        long steps = 4;
        int known = g.rows == 4 && g.cols == 4 && m->npe == 16 &&
                    (!m->out[0] || !strcmp(m->out, "diag"));
        if (m->steps[0] && strcmp(m->steps, "rows") && strcmp(m->steps, "auto"))
            known = known && !dsl_integer(m->steps, 0, UINT8_MAX, &steps);
        for (int i = 0; known && i < m->npe; i++) {
            const dsl_pe *p = &m->pe[i];
            if (p->r < 0 || p->r >= 4 || p->c < 0 || p->c >= 4) { known = 0; break; }
            unsigned bit = 1u << (unsigned)(p->r * 4 + p->c);
            if (seen & bit) { known = 0; break; }
            seen |= bit;
            known = !strcmp(p->op, p->r == p->c ? "add" : "pass") &&
                    !strcmp(p->a, p->r <= p->c ? "north" : "west") &&
                    (p->r == p->c ? !strcmp(p->b, "west") :
                     (!p->b[0] || !strcmp(p->b, "zero")));
        }
        if (known && steps >= 4) {
            for (size_t i = 0; i < n; i++) out[i] = scalar("add", a[i], b[i], 0, 0);
            return (int)n;
        }
    }
    int reduction = !strcmp(m->pattern, "reduce");
    if (m->pattern[0] && strcmp(m->pattern, "diagonal") && !reduction) {
        snprintf(reason, reason_size, "no scalar reference for pattern %s", m->pattern);
        return -1;
    }
    const char *op = m->op[0] ? m->op : reduction ? "mac" : "add";
    int16_t ca = 0, cb = 0;
    enum source sa = source_kind(m->a[0] ? m->a : "north", &ca);
    enum source sb = source_kind(m->b[0] ? m->b : "west", &cb);
    if (sa == UNSUPPORTED || sb == UNSUPPORTED ||
        (!reduction && (sa == SELF || sb == SELF || !strcmp(op, "mac") ||
                        !strcmp(op, "acc") || !strcmp(op, "nop")))) {
        snprintf(reason, reason_size, "no scalar reference for stateful diagonal or neighbor operands");
        return -1;
    }
    int16_t imm = sa == CONSTANT ? ca : cb;
    long steps = reduction ? 1 : (g.rows < g.cols ? g.rows : g.cols);
    if (!strcmp(m->steps, "rows")) steps = g.rows;
    else if (m->steps[0] && strcmp(m->steps, "auto") &&
             dsl_integer(m->steps, 0, UINT8_MAX, &steps)) return -1;
    if (!reduction && steps < (g.rows < g.cols ? g.rows : g.cols)) {
        snprintf(reason, reason_size, "diagonal schedule does not provide enough steps for a scalar reference");
        return -1;
    }
    int16_t state = 0;
    for (size_t i = 0; i < n; ++i) {
        long count = reduction ? steps : 1;
        for (long step = 0; step < count; ++step)
            state = scalar(op, operand(sa, a[i], b[i], ca, state),
                           operand(sb, a[i], b[i], cb, state), state, imm);
        if (!reduction) out[i] = state;
    }
    if (reduction) out[0] = state;
    return reduction ? 1 : (int)n;
}
