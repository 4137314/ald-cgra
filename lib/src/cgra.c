/*
 * cgra.c — serial transport, protocol and accelerated kernels.
 * See hw/rtl/cgra_ctrl.vhd for the device-side protocol FSM.
 */

#define _DEFAULT_SOURCE

#include "cgra.h"
#include "emu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

/* protocol bytes — keep in sync with hw/rtl/cgra_pkg.vhd */
#define CMD_ID   0x01
#define CMD_CFG  0x02
#define CMD_WR   0x03
#define CMD_RUN  0x04
#define CMD_RD   0x05
#define CMD_RST  0x06

#define RSP_ACK  0x79
#define RSP_NACK 0x1F
#define ID_BYTE0 0xCA

struct cgra {
    int          fd;           /* -1 for the sim: emulator backend */
    unsigned     timeout_ms;
    unsigned     retries;      /* retransmissions on NACK/timeout  */
    cgra_emu_t  *emu;          /* non-NULL for the sim: backend    */
    cgra_stats_t stats;
};

/* -------------------------------------------------- serial transport */

static speed_t baud_to_speed(unsigned baud)
{
    switch (baud) {
    case 9600:    return B9600;
    case 19200:   return B19200;
    case 38400:   return B38400;
    case 57600:   return B57600;
    case 115200:  return B115200;
#ifdef B230400
    case 230400:  return B230400;
#endif
#ifdef B460800
    case 460800:  return B460800;
#endif
#ifdef B921600
    case 921600:  return B921600;
#endif
    default:      return 0;
    }
}

cgra_t *cgra_open(const char *device, unsigned baud)
{
    if (device == NULL)
        return NULL;

    /* In-process emulator: device string "sim" or "sim:...". No real port,
     * baud is irrelevant. Lets the whole stack run without an FPGA. */
    if (strcmp(device, "sim") == 0 || strncmp(device, "sim:", 4) == 0) {
        cgra_t *dev = calloc(1, sizeof(*dev));
        if (dev == NULL)
            return NULL;
        dev->emu = emu_new();
        if (dev->emu == NULL) {
            free(dev);
            return NULL;
        }
        /* "sim:flaky" NACKs the first CFG and first WR to exercise retries. */
        if (strstr(device, "flaky") != NULL)
            emu_set_flaky(dev->emu, 1);
        dev->fd = -1;
        dev->timeout_ms = 2000;
        dev->retries = 3;
        return dev;
    }

    speed_t speed = baud_to_speed(baud);
    if (speed == 0)
        return NULL;

    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return NULL;

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return NULL;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | CRTSCTS);   /* 8N1, no flow control */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return NULL;
    }
    tcflush(fd, TCIOFLUSH);

    cgra_t *dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        close(fd);
        return NULL;
    }
    dev->fd = fd;
    dev->timeout_ms = 2000;
    dev->retries = 3;
    return dev;
}

void cgra_close(cgra_t *dev)
{
    if (dev != NULL) {
        if (dev->emu != NULL)
            emu_free(dev->emu);
        else
            close(dev->fd);
        free(dev);
    }
}

void cgra_set_timeout(cgra_t *dev, unsigned ms)
{
    if (dev != NULL)
        dev->timeout_ms = ms;
}

void cgra_set_retries(cgra_t *dev, unsigned retries)
{
    if (dev != NULL)
        dev->retries = retries;
}

void cgra_get_stats(cgra_t *dev, cgra_stats_t *stats)
{
    if (dev != NULL && stats != NULL)
        *stats = dev->stats;
}

void cgra_reset_stats(cgra_t *dev)
{
    if (dev != NULL)
        memset(&dev->stats, 0, sizeof(dev->stats));
}

const char *cgra_strerror(int err)
{
    switch (err) {
    case CGRA_OK:          return "success";
    case CGRA_ERR_IO:      return "I/O error";
    case CGRA_ERR_TIMEOUT: return "timeout waiting for the device";
    case CGRA_ERR_NACK:    return "command rejected by the device";
    case CGRA_ERR_PROTO:   return "protocol error / unexpected reply";
    case CGRA_ERR_ARG:     return "invalid argument";
    default:               return "unknown error";
    }
}

static int write_exact(cgra_t *dev, const uint8_t *buf, size_t n)
{
    dev->stats.tx_bytes += n;
    if (dev->emu != NULL) {
        emu_feed(dev->emu, buf, n);   /* one call: FSM stays inlined in emu.c */
        return CGRA_OK;
    }

    size_t sent = 0;
    while (sent < n) {
        ssize_t k = write(dev->fd, buf + sent, n - sent);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return CGRA_ERR_IO;
        }
        sent += (size_t)k;
    }
    return CGRA_OK;
}

static int read_exact(cgra_t *dev, uint8_t *buf, size_t n)
{
    size_t got = 0;

    dev->stats.rx_bytes += n;
    if (dev->emu != NULL) {
        if (emu_drain(dev->emu, buf, n) != n)
            return CGRA_ERR_TIMEOUT;       /* replies are synchronous: means a bug */
        return CGRA_OK;
    }

    while (got < n) {
        fd_set rfds;
        struct timeval tv = {
            .tv_sec  = dev->timeout_ms / 1000,
            .tv_usec = (dev->timeout_ms % 1000) * 1000,
        };
        FD_ZERO(&rfds);
        FD_SET(dev->fd, &rfds);
        int r = select(dev->fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return CGRA_ERR_IO;
        }
        if (r == 0)
            return CGRA_ERR_TIMEOUT;
        ssize_t k = read(dev->fd, buf + got, n - got);
        if (k <= 0)
            return CGRA_ERR_IO;
        got += (size_t)k;
    }
    return CGRA_OK;
}

static int expect_ack(cgra_t *dev)
{
    uint8_t b;
    int rc = read_exact(dev, &b, 1);
    if (rc != CGRA_OK)
        return rc;
    if (b == RSP_ACK)
        return CGRA_OK;
    if (b == RSP_NACK)
        return CGRA_ERR_NACK;
    return CGRA_ERR_PROTO;
}

/* Drop any stale received bytes before retransmitting. */
static void flush_input(cgra_t *dev)
{
    if (dev->emu != NULL) {
        uint8_t b;
        while (emu_pop(dev->emu, &b)) { }
    } else {
        tcflush(dev->fd, TCIFLUSH);
    }
}

/* Best-effort recovery after a failed/aborted RUN: drop stale input and clear
 * the datapath so the device is left in a known state (not "hanging"). */
static void resync(cgra_t *dev)
{
    flush_input(dev);
    uint8_t cmd = CMD_RST;
    if (write_exact(dev, &cmd, 1) == CGRA_OK) {
        uint8_t b;
        (void)read_exact(dev, &b, 1);   /* consume the ACK if the device replies */
    }
    flush_input(dev);
}

/* Send a command buffer and expect an ACK, retransmitting on NACK/timeout.
 * Used for the idempotent CFG and WR transactions. */
static int send_expect_ack(cgra_t *dev, const uint8_t *buf, size_t n)
{
    int rc = CGRA_ERR_IO;
    dev->stats.transactions++;
    for (unsigned att = 0; att <= dev->retries; att++) {
        if (att > 0) { dev->stats.retries++; flush_input(dev); }
        rc = write_exact(dev, buf, n);
        if (rc != CGRA_OK)
            return rc;
        rc = expect_ack(dev);
        if (rc == CGRA_OK)
            return CGRA_OK;
        if (rc != CGRA_ERR_NACK && rc != CGRA_ERR_TIMEOUT)
            return rc;   /* framing/IO error: not worth retrying */
    }
    return rc;
}

/* -------------------------------------------------- protocol primitives */

/* 8-bit additive checksum over n bytes (matches hw/rtl/cgra_ctrl.vhd). */
static uint8_t checksum(const uint8_t *p, size_t n)
{
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++)
        s = (uint8_t)(s + p[i]);
    return s;
}

int cgra_identify(cgra_t *dev, cgra_info_t *info)
{
    if (dev == NULL)
        return CGRA_ERR_ARG;
    dev->stats.transactions++;
    uint8_t cmd = CMD_ID;
    uint8_t rsp[5];
    int rc = write_exact(dev, &cmd, 1);
    if (rc != CGRA_OK)
        return rc;
    rc = read_exact(dev, rsp, sizeof(rsp));
    if (rc != CGRA_OK)
        return rc;
    if (rsp[0] != ID_BYTE0)
        return CGRA_ERR_PROTO;
    if (info != NULL) {
        info->version = rsp[1];
        info->rows    = rsp[2];
        info->cols    = rsp[3];
        info->data_w  = rsp[4];
    }
    return CGRA_OK;
}

int cgra_ping(cgra_t *dev, uint8_t *version)
{
    cgra_info_t info;
    int rc = cgra_identify(dev, &info);
    if (rc == CGRA_OK && version != NULL)
        *version = info.version;
    return rc;
}

int cgra_configure(cgra_t *dev, const uint32_t cfg[CGRA_NUM_PE])
{
    if (dev == NULL || cfg == NULL)
        return CGRA_ERR_ARG;
    uint8_t buf[1 + 4 * CGRA_NUM_PE + 1];   /* cmd + payload + checksum */
    buf[0] = CMD_CFG;
    for (int i = 0; i < CGRA_NUM_PE; i++) {
        buf[1 + 4 * i + 0] = (uint8_t)(cfg[i] & 0xFF);
        buf[1 + 4 * i + 1] = (uint8_t)((cfg[i] >> 8) & 0xFF);
        buf[1 + 4 * i + 2] = (uint8_t)((cfg[i] >> 16) & 0xFF);
        buf[1 + 4 * i + 3] = (uint8_t)((cfg[i] >> 24) & 0xFF);
    }
    buf[1 + 4 * CGRA_NUM_PE] = checksum(buf + 1, 4 * CGRA_NUM_PE);
    return send_expect_ack(dev, buf, sizeof(buf));
}

int cgra_write_inputs(cgra_t *dev, const int16_t west[CGRA_ROWS],
                      const int16_t north[CGRA_COLS])
{
    if (dev == NULL || west == NULL || north == NULL)
        return CGRA_ERR_ARG;
    uint8_t buf[1 + 2 * (CGRA_ROWS + CGRA_COLS) + 1];
    buf[0] = CMD_WR;
    for (int i = 0; i < CGRA_ROWS; i++) {
        buf[1 + 2 * i + 0] = (uint8_t)((uint16_t)west[i] & 0xFF);
        buf[1 + 2 * i + 1] = (uint8_t)((uint16_t)west[i] >> 8);
    }
    for (int i = 0; i < CGRA_COLS; i++) {
        buf[1 + 2 * (CGRA_ROWS + i) + 0] = (uint8_t)((uint16_t)north[i] & 0xFF);
        buf[1 + 2 * (CGRA_ROWS + i) + 1] = (uint8_t)((uint16_t)north[i] >> 8);
    }
    buf[1 + 2 * (CGRA_ROWS + CGRA_COLS)] = checksum(buf + 1, 2 * (CGRA_ROWS + CGRA_COLS));
    return send_expect_ack(dev, buf, sizeof(buf));
}

int cgra_run(cgra_t *dev, uint8_t steps)
{
    if (dev == NULL)
        return CGRA_ERR_ARG;
    /* RUN advances the (stateful) datapath, so it must NOT be retried. On
     * failure we resync the device instead of leaving it in limbo. */
    dev->stats.transactions++;
    uint8_t buf[2] = { CMD_RUN, steps };
    int rc = write_exact(dev, buf, sizeof(buf));
    if (rc != CGRA_OK) { resync(dev); return rc; }
    rc = expect_ack(dev);
    if (rc != CGRA_OK)
        resync(dev);
    return rc;
}

int cgra_read_regs(cgra_t *dev, int16_t regs[CGRA_NUM_PE])
{
    if (dev == NULL || regs == NULL)
        return CGRA_ERR_ARG;
    uint8_t cmd = CMD_RD;
    uint8_t buf[2 * CGRA_NUM_PE + 1];   /* payload + checksum */
    int rc = CGRA_ERR_IO;
    dev->stats.transactions++;
    /* RD is a pure read, so a bad checksum can be retried safely. */
    for (unsigned att = 0; att <= dev->retries; att++) {
        if (att > 0) { dev->stats.retries++; flush_input(dev); }
        rc = write_exact(dev, &cmd, 1);
        if (rc != CGRA_OK)
            return rc;
        rc = read_exact(dev, buf, sizeof(buf));
        if (rc == CGRA_OK && checksum(buf, 2 * CGRA_NUM_PE) == buf[2 * CGRA_NUM_PE]) {
            for (int i = 0; i < CGRA_NUM_PE; i++)
                regs[i] = (int16_t)((uint16_t)buf[2 * i] | ((uint16_t)buf[2 * i + 1] << 8));
            return CGRA_OK;
        }
        if (rc == CGRA_OK)
            rc = CGRA_ERR_PROTO;      /* checksum mismatch */
        if (rc != CGRA_ERR_PROTO && rc != CGRA_ERR_TIMEOUT)
            return rc;
    }
    return rc;
}

int cgra_reset_datapath(cgra_t *dev)
{
    if (dev == NULL)
        return CGRA_ERR_ARG;
    dev->stats.transactions++;
    uint8_t cmd = CMD_RST;
    int rc = write_exact(dev, &cmd, 1);
    if (rc != CGRA_OK)
        return rc;
    return expect_ack(dev);
}

int cgra_apply(cgra_t *dev, const uint32_t cfg[CGRA_NUM_PE],
               const int16_t west[CGRA_ROWS], const int16_t north[CGRA_COLS],
               uint8_t steps, int16_t regs[CGRA_NUM_PE])
{
    int rc;
    if (dev == NULL)
        return CGRA_ERR_ARG;
    if (cfg != NULL) {
        rc = cgra_configure(dev, cfg);
        if (rc != CGRA_OK) return rc;
    }
    if (west != NULL || north != NULL) {
        static const int16_t zeros_w[CGRA_ROWS] = {0};
        static const int16_t zeros_n[CGRA_COLS] = {0};
        rc = cgra_write_inputs(dev, west ? west : zeros_w, north ? north : zeros_n);
        if (rc != CGRA_OK) return rc;
    }
    rc = cgra_run(dev, steps);
    if (rc != CGRA_OK) return rc;
    if (regs != NULL) {
        rc = cgra_read_regs(dev, regs);
        if (rc != CGRA_OK) return rc;
    }
    return CGRA_OK;
}

/* -------------------------------------------------- accelerated kernels */

/*
 * Diagonal element-wise configuration:
 *   - a[] enters from the north edge (one element per column),
 *   - b[] enters from the west edge (one element per row),
 *   - PEs above the diagonal PASS the a value southwards,
 *   - PEs below the diagonal PASS the b value eastwards,
 *   - the diagonal PE(k,k) computes a[k] <op> b[k].
 * After CGRA_ROWS steps every diagonal register holds its result.
 */
static void diag_cfg(uint32_t cfg[CGRA_NUM_PE], enum cgra_op op,
                     int b_is_imm, int16_t imm)
{
    for (int r = 0; r < CGRA_ROWS; r++) {
        for (int c = 0; c < CGRA_COLS; c++) {
            uint32_t w;
            if (r == c)
                w = CGRA_CFG(op, CGRA_SEL_N,
                             b_is_imm ? CGRA_SEL_CONST : CGRA_SEL_W, imm);
            else if (r < c)
                w = CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_N, CGRA_SEL_ZERO, 0);
            else
                w = CGRA_CFG(CGRA_OP_PASS, CGRA_SEL_W, CGRA_SEL_ZERO, 0);
            cfg[r * CGRA_COLS + c] = w;
        }
    }
}

static int diag_vec_op(cgra_t *dev, enum cgra_op op,
                       const int16_t *a, const int16_t *b, int16_t imm,
                       int16_t *out, size_t n)
{
    if (dev == NULL || a == NULL || out == NULL)
        return CGRA_ERR_ARG;

    uint32_t cfg[CGRA_NUM_PE];
    diag_cfg(cfg, op, b == NULL, imm);
    int rc = cgra_configure(dev, cfg);
    if (rc != CGRA_OK)
        return rc;

    for (size_t i = 0; i < n; i += CGRA_ROWS) {
        int16_t north[CGRA_COLS] = { 0 };
        int16_t west[CGRA_ROWS]  = { 0 };
        int16_t regs[CGRA_NUM_PE];
        size_t m = n - i < CGRA_ROWS ? n - i : CGRA_ROWS;

        for (size_t j = 0; j < m; j++) {
            north[j] = a[i + j];
            if (b != NULL)
                west[j] = b[i + j];
        }
        rc = cgra_write_inputs(dev, west, north);
        if (rc != CGRA_OK)
            return rc;
        rc = cgra_run(dev, CGRA_ROWS);
        if (rc != CGRA_OK)
            return rc;
        rc = cgra_read_regs(dev, regs);
        if (rc != CGRA_OK)
            return rc;
        for (size_t j = 0; j < m; j++)
            out[i + j] = regs[j * CGRA_COLS + j];
    }
    return CGRA_OK;
}

int cgra_vec_binop(cgra_t *dev, enum cgra_op op,
                   const int16_t *a, const int16_t *b, int16_t *out, size_t n)
{
    if (b == NULL)
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
    if (dev == NULL || a == NULL || b == NULL || result == NULL)
        return CGRA_ERR_ARG;

    /* PE(0,0) reads the north and west input ports directly: one MAC per
     * element, accumulated on the device. */
    uint32_t cfg[CGRA_NUM_PE];
    for (int i = 0; i < CGRA_NUM_PE; i++)
        cfg[i] = CGRA_CFG(CGRA_OP_NOP, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 0);
    cfg[0] = CGRA_CFG(CGRA_OP_MAC, CGRA_SEL_N, CGRA_SEL_W, 0);

    int rc = cgra_configure(dev, cfg);
    if (rc != CGRA_OK)
        return rc;
    rc = cgra_reset_datapath(dev);
    if (rc != CGRA_OK)
        return rc;

    for (size_t i = 0; i < n; i++) {
        int16_t north[CGRA_COLS] = { a[i], 0, 0, 0 };
        int16_t west[CGRA_ROWS]  = { b[i], 0, 0, 0 };
        rc = cgra_write_inputs(dev, west, north);
        if (rc != CGRA_OK)
            return rc;
        rc = cgra_run(dev, 1);
        if (rc != CGRA_OK)
            return rc;
    }

    int16_t regs[CGRA_NUM_PE];
    rc = cgra_read_regs(dev, regs);
    if (rc != CGRA_OK)
        return rc;
    *result = regs[0];
    return CGRA_OK;
}
