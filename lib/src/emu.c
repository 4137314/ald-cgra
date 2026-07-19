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
#define RSP_ACK   0x79
#define RSP_NACK  0x1F
#define ID_BYTE0  0xCA
#define PROTO_VER 0x02

#define OUT_CAP 256

struct cgra_emu {
    /* datapath state */
    uint32_t cfg[CGRA_NUM_PE];
    int16_t  regs[CGRA_NUM_PE];
    int16_t  west[CGRA_ROWS];
    int16_t  north[CGRA_COLS];

    /* protocol FSM: current command and how many payload bytes are still due */
    int      cmd;
    int      need;
    int      got;
    uint8_t  buf[4 * CGRA_NUM_PE + 1];   /* payload + trailing checksum byte */

    /* device->host FIFO */
    uint8_t  out[OUT_CAP];
    int      head, tail;

    /* test hooks: NACK the first CFG / WR to force a retry */
    int      flaky_cfg;
    int      flaky_wr;
};

void emu_set_flaky(cgra_emu_t *e, int enable)
{
    e->flaky_cfg = enable;
    e->flaky_wr  = enable;
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
    cgra_emu_t *e = calloc(1, sizeof(*e));
    if (e == NULL)
        return NULL;
    e->cmd = -1;
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
    case CGRA_OP_SHL:   return (int16_t)(a << (b & 0xF));
    case CGRA_OP_SHR:   return (int16_t)(a >> (b & 0xF));   /* arithmetic: a is signed */
    case CGRA_OP_MAX:   return a >= b ? a : b;
    case CGRA_OP_MIN:   return a <= b ? a : b;
    case CGRA_OP_ABS:   return (int16_t)(a < 0 ? -a : a);
    case CGRA_OP_ACC:   return (int16_t)(r + a);
    case CGRA_OP_CONST: return imm;
    default:            return r;
    }
}

static void step_array(cgra_emu_t *e)
{
    int16_t next[CGRA_NUM_PE];

    for (int r = 0; r < CGRA_ROWS; r++) {
        for (int c = 0; c < CGRA_COLS; c++) {
            int idx = r * CGRA_COLS + c;
            int16_t n = (r == 0)             ? e->north[c] : e->regs[(r - 1) * CGRA_COLS + c];
            int16_t s = (r == CGRA_ROWS - 1) ? 0           : e->regs[(r + 1) * CGRA_COLS + c];
            int16_t w = (c == 0)             ? e->west[r]  : e->regs[r * CGRA_COLS + c - 1];
            int16_t ee = (c == CGRA_COLS - 1) ? 0          : e->regs[r * CGRA_COLS + c + 1];

            uint32_t cw = e->cfg[idx];
            int16_t imm  = (int16_t)(cw & 0xFFFF);
            int     op   = (cw >> 16) & 0xF;
            int     sela = (cw >> 20) & 0x7;
            int     selb = (cw >> 23) & 0x7;
            int16_t self = e->regs[idx];

            int16_t a = operand(sela, n, s, ee, w, imm, self);
            int16_t b = operand(selb, n, s, ee, w, imm, self);
            next[idx] = alu(op, a, b, self, imm);
        }
    }
    memcpy(e->regs, next, sizeof(next));
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

static void run_command(cgra_emu_t *e)
{
    switch (e->cmd) {
    case CMD_CFG: {
        int payload = 4 * CGRA_NUM_PE;
        if (checksum(e->buf, payload) != e->buf[payload]) {
            enq(e, RSP_NACK);
            break;
        }
        if (e->flaky_cfg) {          /* test hook: force one retry */
            e->flaky_cfg = 0;
            enq(e, RSP_NACK);
            break;
        }
        for (int i = 0; i < CGRA_NUM_PE; i++)
            e->cfg[i] = (uint32_t)e->buf[4 * i]
                      | ((uint32_t)e->buf[4 * i + 1] << 8)
                      | ((uint32_t)e->buf[4 * i + 2] << 16)
                      | ((uint32_t)e->buf[4 * i + 3] << 24);
        enq(e, RSP_ACK);
        break;
    }
    case CMD_WR: {
        int payload = 2 * (CGRA_ROWS + CGRA_COLS);
        if (checksum(e->buf, payload) != e->buf[payload]) {
            enq(e, RSP_NACK);
            break;
        }
        if (e->flaky_wr) {           /* test hook: force one retry */
            e->flaky_wr = 0;
            enq(e, RSP_NACK);
            break;
        }
        for (int i = 0; i < CGRA_ROWS; i++)
            e->west[i] = (int16_t)((uint16_t)e->buf[2 * i] | ((uint16_t)e->buf[2 * i + 1] << 8));
        for (int i = 0; i < CGRA_COLS; i++) {
            int j = CGRA_ROWS + i;
            e->north[i] = (int16_t)((uint16_t)e->buf[2 * j] | ((uint16_t)e->buf[2 * j + 1] << 8));
        }
        enq(e, RSP_ACK);
        break;
    }
    case CMD_RUN:
        for (int k = 0; k < e->buf[0]; k++)
            step_array(e);
        enq(e, RSP_ACK);
        break;

    default:
        break;
    }
}

void emu_push(cgra_emu_t *e, uint8_t byte)
{
    if (e->cmd < 0) {
        /* waiting for a command byte */
        switch (byte) {
        case CMD_ID:
            enq(e, ID_BYTE0);
            enq(e, PROTO_VER);
            enq(e, (uint8_t)CGRA_ROWS);
            enq(e, (uint8_t)CGRA_COLS);
            enq(e, (uint8_t)CGRA_DATA_W);
            break;
        case CMD_RD: {
            uint8_t ck = 0;
            for (int i = 0; i < CGRA_NUM_PE; i++) {
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
            e->cmd = CMD_CFG; e->need = 4 * CGRA_NUM_PE + 1; e->got = 0;
            break;
        case CMD_WR:
            e->cmd = CMD_WR;  e->need = 2 * (CGRA_ROWS + CGRA_COLS) + 1; e->got = 0;
            break;
        case CMD_RUN:
            e->cmd = CMD_RUN; e->need = 1; e->got = 0;
            break;
        default:
            enq(e, RSP_NACK);
            break;
        }
    } else {
        /* collecting payload bytes for the current command */
        e->buf[e->got++] = byte;
        if (e->got >= e->need) {
            run_command(e);
            e->cmd = -1;
        }
    }
}
