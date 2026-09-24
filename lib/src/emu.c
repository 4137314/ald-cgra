/*
 * emu.c — software model of the CGRA device.
 *
 * Mirrors the datapath of hw/rtl/pe.vhd + cgra_array.vhd and the protocol
 * FSM of hw/rtl/cgra_ctrl.vhd.  Keep it in sync with those files: it doubles
 * as the golden reference for the hardware.
 */

#include "emu.h"
#include "cgra.h"

#include <stdlib.h>
#include <string.h>

/* protocol bytes — keep in sync with hw/rtl/cgra_pkg.vhd and cgra.c */
#define CMD_ID   0x01
#define CMD_CFG  0x02
#define CMD_WR   0x03
#define CMD_RUN  0x04
#define CMD_RD   0x05
#define CMD_RST  0x06
#define CMD_EXEC 0x07
#define RSP_ACK   0x79
#define RSP_NACK  0x1F
#define ID_BYTE0  0xCA
#define PROTO_VER 0x03

/* CMD_EXEC payload: steps, flags, mask_lo, mask_hi, then the CMD_WR edge
 * inputs, then the checksum. */
#define EXEC_HDR  4
#define EXEC_LEN(e) (EXEC_HDR + 2 * ((e)->rows + (e)->cols) + 1)

#define OUT_CAP 256

/* A configuration word split into its fields once, when it is loaded, instead
 * of on every PE on every step -- step_array() is by far the hottest loop in
 * the emulator (a 1024-element element-wise kernel steps 16 PEs 1024 times),
 * and re-deriving four bit fields per PE per step was pure overhead. Mirrors
 * decode_cfg() in hw/rtl/cgra_pkg.vhd, which the hardware likewise evaluates
 * once, combinationally, off the held configuration register. */
typedef struct {
    int16_t imm;
    uint8_t op, sela, selb;
} pe_cfg_t;

struct cgra_emu {
    int rows, cols, num_pe;
    /* datapath state */
    uint32_t cfg[CGRA_MAX_PE];
    pe_cfg_t dec[CGRA_MAX_PE];      /* cfg[], pre-decoded (see above) */
    int16_t  regs[CGRA_MAX_PE];
    int16_t  west[CGRA_MAX_EDGE];
    int16_t  north[CGRA_MAX_EDGE];

    /* protocol FSM: current command and how many payload bytes are still due */
    int      cmd;
    int      need;
    int      got;
    uint8_t  buf[4 * CGRA_MAX_PE + 1];   /* payload + trailing checksum byte */

    /* device->host FIFO */
    uint8_t  out[OUT_CAP];
    int      head, tail;

    /* test hooks: NACK the first CFG and the first input-carrying transaction
     * (CMD_WR or CMD_EXEC, whichever the host uses) to force a retry */
    int      flaky_cfg;
    int      flaky_wr;

    /* test hook: behave like a protocol v2 device (no CMD_EXEC) */
    int      legacy;
};

void emu_set_flaky(cgra_emu_t *e, int enable)
{
    e->flaky_cfg = enable;
    e->flaky_wr  = enable;
}

void emu_set_legacy(cgra_emu_t *e, int enable)
{
    e->legacy = enable;
}

static void enq(cgra_emu_t *e, uint8_t b)
{
    int n = (e->tail + 1) % OUT_CAP;
    if (n != e->head) {           /* drop on overflow, never happens in practice */
        e->out[e->tail] = b;
        e->tail = n;
    }
}

int emu_pop(cgra_emu_t *e, uint8_t *byte)
{
    if (e->head == e->tail)
        return 0;
    *byte = e->out[e->head];
    e->head = (e->head + 1) % OUT_CAP;
    return 1;
}

cgra_emu_t *emu_new(void)
{
    return emu_new_geometry(CGRA_ROWS, CGRA_COLS);
}

cgra_emu_t *emu_new_geometry(unsigned rows, unsigned cols)
{
    if (rows == 0 || cols == 0 || rows > CGRA_MAX_EDGE ||
        cols > CGRA_MAX_EDGE || rows * cols > CGRA_MAX_PE)
        return NULL;
    cgra_emu_t *e = calloc(1, sizeof(*e));
    if (e == NULL)
        return NULL;
    e->cmd = -1;
    e->rows = (int)rows;
    e->cols = (int)cols;
    e->num_pe = (int)(rows * cols);
    return e;
}

void emu_free(cgra_emu_t *e)
{
    free(e);
}

/* -------------------------------------------------- PE datapath model */

static int16_t operand(int sel, int16_t n, int16_t s, int16_t e,
                       int16_t w, int16_t imm, int16_t self)
{
    switch (sel) {
    case CGRA_SEL_N:     return n;
    case CGRA_SEL_S:     return s;
    case CGRA_SEL_E:     return e;
    case CGRA_SEL_W:     return w;
    case CGRA_SEL_CONST: return imm;
    case CGRA_SEL_SELF:  return self;
    default:             return 0;   /* SEL_ZERO */
    }
}

static int16_t alu(int op, int16_t a, int16_t b, int16_t r, int16_t imm)
{
    switch (op) {
    case CGRA_OP_NOP:   return r;
    case CGRA_OP_PASS:  return a;
    case CGRA_OP_ADD:   return (int16_t)(a + b);
    case CGRA_OP_SUB:   return (int16_t)(a - b);
    case CGRA_OP_MUL:   return (int16_t)(a * b);
    case CGRA_OP_MAC:   return (int16_t)(r + (int16_t)(a * b));
    case CGRA_OP_AND:   return (int16_t)(a & b);
    case CGRA_OP_OR:    return (int16_t)(a | b);
    case CGRA_OP_XOR:   return (int16_t)(a ^ b);
    case CGRA_OP_SHL:   return (int16_t)((uint16_t)a << (b & 0xF)); /* shift in unsigned: defined 2's-complement wrap */
    case CGRA_OP_SHR:   return (int16_t)(a >> (b & 0xF));   /* arithmetic: a is signed */
    case CGRA_OP_MAX:   return a >= b ? a : b;
    case CGRA_OP_MIN:   return a <= b ? a : b;
    case CGRA_OP_ABS:   return (int16_t)(a < 0 ? -a : a);
    case CGRA_OP_ACC:   return (int16_t)(r + a);
    case CGRA_OP_CONST: return imm;
    default:            return r;
    }
}

/* Split one configuration word into the fields step_array() consumes. */
static void decode_cfg(pe_cfg_t *d, uint32_t cw)
{
    d->imm  = (int16_t)(cw & 0xFFFF);
    d->op   = (uint8_t)((cw >> 16) & 0xF);
    d->sela = (uint8_t)((cw >> 20) & 0x7);
    d->selb = (uint8_t)((cw >> 23) & 0x7);
}

static void step_array(cgra_emu_t *e)
{
    int16_t next[CGRA_MAX_PE];

    for (int r = 0; r < e->rows; r++) {
        for (int c = 0; c < e->cols; c++) {
            int idx = r * e->cols + c;
            int16_t n = (r == 0)             ? e->north[c] : e->regs[(r - 1) * e->cols + c];
            int16_t s = (r == e->rows - 1) ? 0           : e->regs[(r + 1) * e->cols + c];
            int16_t w = (c == 0)             ? e->west[r]  : e->regs[r * e->cols + c - 1];
            int16_t ee = (c == e->cols - 1) ? 0          : e->regs[r * e->cols + c + 1];

            const pe_cfg_t *d = &e->dec[idx];
            int16_t self = e->regs[idx];

            int16_t a = operand(d->sela, n, s, ee, w, d->imm, self);
            int16_t b = operand(d->selb, n, s, ee, w, d->imm, self);
            next[idx] = alu(d->op, a, b, self, d->imm);
        }
    }
    memcpy(e->regs, next, (size_t)e->num_pe * sizeof(next[0]));
}

/* -------------------------------------------------- protocol FSM */

/* 8-bit additive checksum over the first n payload bytes. */
static uint8_t checksum(const uint8_t *p, int n)
{
    uint8_t s = 0;
    for (int i = 0; i < n; i++)
        s = (uint8_t)(s + p[i]);
    return s;
}

/* RTL CFG/WR registers latch each complete word before the checksum arrives.
 * A NACK (or a truncated request) does not roll those writes back. Keep both
 * feed paths faithful to that streaming behavior, including EXEC's inputs. */
static void latch_payload(cgra_emu_t *e, int previous)
{
    if (e->cmd == CMD_CFG) {
        int end = e->got / 4;
        if (end > e->num_pe) end = e->num_pe;
        for (int i = previous / 4; i < end; ++i) {
            e->cfg[i] = (uint32_t)e->buf[4 * i]
                      | ((uint32_t)e->buf[4 * i + 1] << 8)
                      | ((uint32_t)e->buf[4 * i + 2] << 16)
                      | ((uint32_t)e->buf[4 * i + 3] << 24);
            decode_cfg(&e->dec[i], e->cfg[i]);
        }
    } else if (e->cmd == CMD_WR || e->cmd == CMD_EXEC) {
        int offset = e->cmd == CMD_EXEC ? EXEC_HDR : 0;
        int begin = previous > offset ? (previous - offset) / 2 : 0;
        int end = e->got > offset ? (e->got - offset) / 2 : 0;
        if (end > e->rows + e->cols) end = e->rows + e->cols;
        for (int i = begin; i < end; ++i) {
            const uint8_t *p = e->buf + offset + 2 * i;
            int16_t value = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            if (i < e->rows) e->west[i] = value;
            else e->north[i - e->rows] = value;
        }
    }
}

static void run_command(cgra_emu_t *e)
{
    switch (e->cmd) {
    case CMD_CFG: {
        int payload = 4 * e->num_pe;
        if (checksum(e->buf, payload) != e->buf[payload]) {
            enq(e, RSP_NACK);
            break;
        }
        if (e->flaky_cfg) {          /* test hook: force one retry */
            e->flaky_cfg = 0;
            enq(e, RSP_NACK);
            break;
        }
        enq(e, RSP_ACK);
        break;
    }
    case CMD_WR: {
        int payload = 2 * (e->rows + e->cols);
        if (checksum(e->buf, payload) != e->buf[payload]) {
            enq(e, RSP_NACK);
            break;
        }
        if (e->flaky_wr) {           /* test hook: force one retry */
            e->flaky_wr = 0;
            enq(e, RSP_NACK);
            break;
        }
        enq(e, RSP_ACK);
        break;
    }
    case CMD_RUN:
        for (int k = 0; k < e->buf[0]; k++)
            step_array(e);
        enq(e, RSP_ACK);
        break;

    /* Protocol v3: fused write-inputs + run + masked read-back, one round
     * trip. Mirrors cgra_ctrl.vhd states S_EX_HDR/S_WR/S_EX_CK/S_EX_RD:
     * the edge inputs are latched as they arrive (exactly as CMD_WR does,
     * before its checksum is known), but the array is only stepped when the
     * checksum is good. The reply is always the same length -- taps, data
     * checksum, status -- so the host can size its read from the mask alone. */
    case CMD_EXEC: {
        int      payload = EXEC_LEN(e) - 1;
        int      steps   = e->buf[0];
        unsigned flags   = e->buf[1];
        uint16_t mask    = (uint16_t)(e->buf[2] | ((uint16_t)e->buf[3] << 8));
        int      ok      = checksum(e->buf, payload) == e->buf[payload];

        if (ok && e->flaky_wr) {      /* test hook: force one retry */
            e->flaky_wr = 0;
            ok = 0;
        }


        if (ok) {
            if (flags & CGRA_EXEC_RESET)
                memset(e->regs, 0, sizeof(e->regs));
            for (int k = 0; k < steps; k++)
                step_array(e);
        }

        uint8_t ck = 0;
        for (int i = 0; i < e->num_pe; i++) {
            if (((mask >> i) & 1) == 0)
                continue;
            uint8_t lo = (uint8_t)(e->regs[i] & 0xFF);
            uint8_t hi = (uint8_t)((uint16_t)e->regs[i] >> 8);
            enq(e, lo); enq(e, hi);
            ck = (uint8_t)(ck + lo + hi);
        }
        enq(e, ck);
        enq(e, ok ? RSP_ACK : RSP_NACK);
        break;
    }

    default:
        break;
    }
}

/* One host->device byte through the FSM. Static so emu_feed inlines it. */
static void push_byte(cgra_emu_t *e, uint8_t byte)
{
    if (e->cmd < 0) {
        /* waiting for a command byte */
        switch (byte) {
        case CMD_ID:
            enq(e, ID_BYTE0);
            enq(e, e->legacy ? 0x02 : PROTO_VER);
            enq(e, (uint8_t)e->rows);
            enq(e, (uint8_t)e->cols);
            enq(e, (uint8_t)CGRA_DATA_W);
            break;
        case CMD_RD: {
            uint8_t ck = 0;
            for (int i = 0; i < e->num_pe; i++) {
                uint8_t lo = (uint8_t)(e->regs[i] & 0xFF);
                uint8_t hi = (uint8_t)((uint16_t)e->regs[i] >> 8);
                enq(e, lo); enq(e, hi);
                ck = (uint8_t)(ck + lo + hi);
            }
            enq(e, ck);
            break;
        }
        case CMD_RST:
            memset(e->regs, 0, sizeof(e->regs));
            enq(e, RSP_ACK);
            break;
        case CMD_CFG:
            e->cmd = CMD_CFG; e->need = 4 * e->num_pe + 1; e->got = 0;
            break;
        case CMD_WR:
            e->cmd = CMD_WR;  e->need = 2 * (e->rows + e->cols) + 1; e->got = 0;
            break;
        case CMD_RUN:
            e->cmd = CMD_RUN; e->need = 1; e->got = 0;
            break;
        case CMD_EXEC:
            if (e->legacy) {          /* a v2 device does not know this byte */
                enq(e, RSP_NACK);
                break;
            }
            e->cmd = CMD_EXEC; e->need = EXEC_LEN(e); e->got = 0;
            break;
        default:
            enq(e, RSP_NACK);
            break;
        }
    } else {
        /* collecting payload bytes for the current command */
        e->buf[e->got++] = byte;
        latch_payload(e, e->got - 1);
        if (e->got >= e->need) {
            run_command(e);
            e->cmd = -1;
        }
    }
}

void emu_push(cgra_emu_t *e, uint8_t byte)
{
    push_byte(e, byte);
}

void emu_feed(cgra_emu_t *e, const uint8_t *buf, size_t n)
{
    size_t i = 0;
    while (i < n) {
        if (e->cmd < 0) {
            /* command byte: dispatch (sets cmd/need, or replies immediately) */
            push_byte(e, buf[i++]);
        } else {
            /* payload: bulk-copy the whole span still due in one memcpy
             * instead of one FSM step per byte (identical result). */
            size_t due  = (size_t)(e->need - e->got);
            size_t take = due < (n - i) ? due : (n - i);
            int previous = e->got;
            memcpy(&e->buf[e->got], &buf[i], take);
            e->got += (int)take;
            latch_payload(e, previous);
            i      += take;
            if (e->got >= e->need) {
                run_command(e);
                e->cmd = -1;
            }
        }
    }
}

size_t emu_drain(cgra_emu_t *e, uint8_t *buf, size_t n)
{
    size_t avail = (size_t)((e->tail - e->head + OUT_CAP) % OUT_CAP);
    size_t take  = n < avail ? n : avail;
    /* copy the (up to two) contiguous spans of the ring with memcpy */
    size_t first = (size_t)(OUT_CAP - e->head);
    if (first > take)
        first = take;
    memcpy(buf, &e->out[e->head], first);
    if (take > first)
        memcpy(buf + first, &e->out[0], take - first);
    e->head = (int)(((size_t)e->head + take) % OUT_CAP);
    return take;
}
