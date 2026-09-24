/*
 * cgra.c — serial transport, protocol and accelerated kernels.
 * See hw/rtl/cgra_ctrl.vhd for the device-side protocol FSM.
 */

#define _DEFAULT_SOURCE

#include "cgra.h"
#include "buffers.h"
#include "emu.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* protocol bytes — keep in sync with hw/rtl/cgra_pkg.vhd */
#define CMD_ID   0x01
#define CMD_CFG  0x02
#define CMD_WR   0x03
#define CMD_RUN  0x04
#define CMD_RD   0x05
#define CMD_RST  0x06
#define CMD_EXEC 0x07

#define RSP_ACK  0x79
#define RSP_NACK 0x1F
#define ID_BYTE0 0xCA

/* CMD_EXEC request: cmd, steps, flags, mask (2), edge inputs, checksum. */
#define EXEC_HDR     4
#define EXEC_REQ_MAX (1 + EXEC_HDR + 4 * CGRA_MAX_EDGE + 1)

struct cgra {
    int          fd;           /* -1 for the sim: emulator backend */
    unsigned     timeout_ms;
    unsigned     retries;      /* retransmissions on complete NACK */
    int          desynced;     /* no further commands until close */
    cgra_emu_t  *emu;          /* non-NULL for the sim: backend    */
    int          proto_ver;    /* cached ID version, -1 = not asked yet */
    cgra_info_t  info;
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

static int parse_sim(const char *device, unsigned *rows, unsigned *cols,
                     int *legacy, int *flaky)
{
    *rows = CGRA_ROWS; *cols = CGRA_COLS;
    *legacy = 0; *flaky = 0;
    if (strcmp(device, "sim") == 0) return 1;
    char spec[64];
    if (strlen(device + 4) >= sizeof(spec)) return 0;
    strcpy(spec, device + 4);
    char *save = NULL;
    int geometry_seen = 0;
    for (char *token = strtok_r(spec, ":", &save); token;
         token = strtok_r(NULL, ":", &save)) {
        if (strcmp(token, "v2") == 0) { *legacy = 1; continue; }
        if (strcmp(token, "flaky") == 0) { *flaky = 1; continue; }
        if (geometry_seen || token[0] < '1' || token[0] > '9') return 0;
        char *end;
        errno = 0;
        unsigned long r = strtoul(token, &end, 10);
        if (errno || r > CGRA_MAX_EDGE || *end != 'x') return 0;
        token = end + 1;
        if (token[0] < '1' || token[0] > '9') return 0;
        unsigned long c = strtoul(token, &end, 10);
        if (errno || *end || c > CGRA_MAX_EDGE || r * c > CGRA_MAX_PE) return 0;
        *rows = (unsigned)r; *cols = (unsigned)c;
        geometry_seen = 1;
    }
    return 1;
}

cgra_t *cgra_open(const char *device, unsigned baud)
{
    if (device == NULL) {
        errno = EINVAL;
        return NULL;
    }

    /* In-process emulator: device string "sim" or "sim:...". No real port,
     * baud is irrelevant. Lets the whole stack run without an FPGA. */
    if (strcmp(device, "sim") == 0 || strncmp(device, "sim:", 4) == 0) {
        unsigned rows, cols;
        int legacy, flaky;
        if (!parse_sim(device, &rows, &cols, &legacy, &flaky)) { errno = EINVAL; return NULL; }
        cgra_t *dev = calloc(1, sizeof(*dev));
        if (dev == NULL)
            return NULL;
        dev->emu = emu_new_geometry(rows, cols);
        if (dev->emu == NULL) {
            free(dev);
            errno = ENOMEM;
            return NULL;
        }
        /* "sim:flaky" NACKs the first CFG and the first input-carrying
         * transaction to exercise retries; "sim:v2" reports protocol v2 and
         * rejects CMD_EXEC, which keeps the pre-v3 fallback path tested (and
         * makes the fused transaction's saving measurable side by side). */
        if (flaky)
            emu_set_flaky(dev->emu, 1);
        if (legacy)
            emu_set_legacy(dev->emu, 1);
        dev->fd = -1;
        dev->timeout_ms = 2000;
        dev->retries = 3;
        dev->proto_ver = -1;
        return dev;
    }

    speed_t speed = baud_to_speed(baud);
    if (speed == 0) {
        errno = EINVAL;
        return NULL;
    }

    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return NULL;

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return NULL;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | CRTSCTS);   /* 8N1, no flow control */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0 ||
        tcsetattr(fd, TCSANOW, &tio) != 0 || tcflush(fd, TCIOFLUSH) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return NULL;
    }

    cgra_t *dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    dev->fd = fd;
    dev->timeout_ms = 2000;
    dev->retries = 3;
    dev->proto_ver = -1;
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
    case CGRA_ERR_GEOMETRY: return "unsupported or mismatched device geometry";
    case CGRA_ERR_DESYNC:   return "session desynchronized; restore device parser before reopening";
    case CGRA_ERR_NOMEM:    return "memory allocation failed";
    default:               return "unknown error";
    }
}

/* A transport failure leaves both the parser boundary and datapath state
 * uncertain. Sending RST here is unsafe: it can be a missing RUN argument.
 * Keep the original error for this call; later calls fail without sending. */
static int uncertain(cgra_t *dev, int rc)
{
    dev->desynced = 1;
    dev->proto_ver = -1;
    memset(&dev->info, 0, sizeof(dev->info));
    return rc;
}

static int write_exact(cgra_t *dev, const uint8_t *buf, size_t n)
{
    if (dev->emu != NULL) {
        emu_feed(dev->emu, buf, n);   /* one call: FSM stays inlined in emu.c */
        dev->stats.tx_bytes += n;
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
        if (k == 0) return CGRA_ERR_IO;
        sent += (size_t)k;
        dev->stats.tx_bytes += (size_t)k;
    }
    return CGRA_OK;
}

static int read_exact(cgra_t *dev, uint8_t *buf, size_t n)
{
    size_t got = 0;

    if (dev->emu != NULL) {
        got = emu_drain(dev->emu, buf, n);
        dev->stats.rx_bytes += got;
        if (got != n)
            return CGRA_ERR_TIMEOUT;       /* replies are synchronous: means a bug */
        return CGRA_OK;
    }

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return CGRA_ERR_IO;
    const int64_t deadline = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec +
                             (int64_t)dev->timeout_ms * 1000000;
    int first_poll = 1;
    while (got < n) {
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return CGRA_ERR_IO;
        int64_t remaining = deadline - ((int64_t)now.tv_sec * 1000000000 + now.tv_nsec);
        if (!first_poll && remaining <= 0) return CGRA_ERR_TIMEOUT;
        first_poll = 0;
        /* Round up to poll's millisecond resolution, without overflowing its
         * signed timeout argument. A zero timeout still gets one ready check. */
        int64_t ms = remaining > 0 ? (remaining + 999999) / 1000000 : 0;
        struct pollfd pfd = {.fd = dev->fd, .events = POLLIN};
        int r = poll(&pfd, 1, ms > INT_MAX ? INT_MAX : (int)ms);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return CGRA_ERR_IO;
        }
        if (r == 0) continue;   /* re-check the deadline, including very long waits */
        if (!(pfd.revents & POLLIN)) return CGRA_ERR_IO;
        ssize_t k = read(dev->fd, buf + got, n - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0)
            return CGRA_ERR_IO;
        got += (size_t)k;
        dev->stats.rx_bytes += (size_t)k;
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

/* Retry complete NACK replies only. This assumes command boundaries were
 * preserved (see the documented fault model); timeout/IO/unexpected replies
 * never trigger a retransmission or a recovery byte. */
static int send_expect_ack(cgra_t *dev, const uint8_t *buf, size_t n)
{
    if (dev->desynced) return CGRA_ERR_DESYNC;
    dev->stats.transactions++;
    unsigned att = 0;
    for (;;) {
        if (att > 0) dev->stats.retries++;
        int rc = write_exact(dev, buf, n);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        rc = expect_ack(dev);
        if (rc == CGRA_OK) return rc;
        if (rc != CGRA_ERR_NACK) return uncertain(dev, rc);
        if (att == dev->retries) return rc;
        ++att;
    }
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
    if (dev->desynced) return CGRA_ERR_DESYNC;
    dev->stats.transactions++;
    uint8_t cmd = CMD_ID;
    uint8_t rsp[5];
    int rc = write_exact(dev, &cmd, 1);
    if (rc != CGRA_OK)
        return uncertain(dev, rc);
    rc = read_exact(dev, rsp, sizeof(rsp));
    if (rc != CGRA_OK)
        return uncertain(dev, rc);
    if (rsp[0] != ID_BYTE0)
        return uncertain(dev, CGRA_ERR_PROTO);
    dev->proto_ver = rsp[1];      /* cached for cgra_has_exec() */
    dev->info = (cgra_info_t){rsp[1], rsp[2], rsp[3], rsp[4]};
    if (info != NULL) *info = dev->info;
    return CGRA_OK;
}

int cgra_get_info(cgra_t *dev, cgra_info_t *info)
{
    if (dev == NULL) return CGRA_ERR_ARG;
    if (dev->desynced) return CGRA_ERR_DESYNC;
    if (dev->proto_ver < 0) {
        int rc = cgra_identify(dev, NULL);
        if (rc != CGRA_OK) return rc;
    }
    const cgra_info_t *g = &dev->info;
    if (g->version < 2 || g->version > CGRA_PROTO_VER) return CGRA_ERR_PROTO;
    if (g->rows == 0 || g->cols == 0 || g->data_w != CGRA_DATA_W ||
        g->rows * g->cols > CGRA_MAX_PE) return CGRA_ERR_GEOMETRY;
    if (info != NULL) *info = *g;
    return CGRA_OK;
}

static int require_default_geometry(cgra_t *dev)
{
    cgra_info_t info;
    int rc = cgra_get_info(dev, &info);
    if (rc != CGRA_OK) return rc;
    return info.rows == CGRA_ROWS && info.cols == CGRA_COLS
        ? CGRA_OK : CGRA_ERR_GEOMETRY;
}

int cgra_ping(cgra_t *dev, uint8_t *version)
{
    cgra_info_t info;
    int rc = cgra_identify(dev, &info);
    if (rc == CGRA_OK && version != NULL)
        *version = info.version;
    return rc;
}

int cgra_configure_n(cgra_t *dev, const uint32_t *cfg, size_t ncfg)
{
    if (dev == NULL || cfg == NULL) return CGRA_ERR_ARG;
    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;
    if (ncfg != (size_t)g.rows * g.cols) return CGRA_ERR_ARG;
    uint8_t buf[1 + 4 * CGRA_MAX_PE + 1];
    size_t len = 1 + 4 * ncfg + 1;
    buf[0] = CMD_CFG;
    for (size_t i = 0; i < ncfg; ++i)
        for (unsigned j = 0; j < 4; ++j)
            buf[1 + 4 * i + j] = (uint8_t)(cfg[i] >> (8 * j));
    buf[len - 1] = checksum(buf + 1, len - 2);
    return send_expect_ack(dev, buf, len);
}

int cgra_configure(cgra_t *dev, const uint32_t cfg[CGRA_NUM_PE])
{
    if (cfg == NULL) return CGRA_ERR_ARG;
    int rc = require_default_geometry(dev);
    return rc == CGRA_OK ? cgra_configure_n(dev, cfg, CGRA_NUM_PE) : rc;
}

int cgra_write_inputs_n(cgra_t *dev, const int16_t *west, size_t nwest,
                        const int16_t *north, size_t nnorth)
{
    if (dev == NULL || west == NULL || north == NULL) return CGRA_ERR_ARG;
    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;
    if (nwest != g.rows || nnorth != g.cols) return CGRA_ERR_ARG;
    uint8_t buf[1 + 4 * CGRA_MAX_EDGE + 1];
    size_t len = 1 + 2 * (nwest + nnorth) + 1;
    buf[0] = CMD_WR;
    for (size_t i = 0; i < nwest + nnorth; ++i) {
        uint16_t value = (uint16_t)(i < nwest ? west[i] : north[i - nwest]);
        buf[1 + 2 * i] = (uint8_t)value;
        buf[2 + 2 * i] = (uint8_t)(value >> 8);
    }
    buf[len - 1] = checksum(buf + 1, len - 2);
    return send_expect_ack(dev, buf, len);
}

int cgra_write_inputs(cgra_t *dev, const int16_t west[CGRA_ROWS],
                      const int16_t north[CGRA_COLS])
{
    if (west == NULL || north == NULL) return CGRA_ERR_ARG;
    int rc = require_default_geometry(dev);
    return rc == CGRA_OK ? cgra_write_inputs_n(dev, west, CGRA_ROWS, north, CGRA_COLS) : rc;
}

int cgra_run(cgra_t *dev, uint8_t steps)
{
    if (dev == NULL)
        return CGRA_ERR_ARG;
    if (dev->desynced) return CGRA_ERR_DESYNC;
    /* RUN advances state and is never retried, even on a NACK. */
    dev->stats.transactions++;
    uint8_t buf[2] = { CMD_RUN, steps };
    int rc = write_exact(dev, buf, sizeof(buf));
    if (rc != CGRA_OK) return uncertain(dev, rc);
    rc = expect_ack(dev);
    return rc == CGRA_OK ? rc : uncertain(dev, rc);
}

int cgra_read_regs_n(cgra_t *dev, int16_t *regs, size_t capacity)
{
    if (dev == NULL || regs == NULL)
        return CGRA_ERR_ARG;
    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;
    size_t count = (size_t)g.rows * g.cols;
    if (capacity < count) return CGRA_ERR_ARG;
    uint8_t cmd = CMD_RD;
    uint8_t buf[2 * CGRA_MAX_PE + 1];   /* payload + checksum */
    dev->stats.transactions++;
    rc = write_exact(dev, &cmd, 1);
    if (rc != CGRA_OK) return uncertain(dev, rc);
    rc = read_exact(dev, buf, 2 * count + 1);
    if (rc != CGRA_OK) return uncertain(dev, rc);
    if (checksum(buf, 2 * count) != buf[2 * count])
        return uncertain(dev, CGRA_ERR_PROTO);
    for (size_t i = 0; i < count; i++)
        regs[i] = (int16_t)((uint16_t)buf[2 * i] | ((uint16_t)buf[2 * i + 1] << 8));
    return CGRA_OK;
}

int cgra_read_regs(cgra_t *dev, int16_t regs[CGRA_NUM_PE])
{
    if (regs == NULL) return CGRA_ERR_ARG;
    int rc = require_default_geometry(dev);
    return rc == CGRA_OK ? cgra_read_regs_n(dev, regs, CGRA_NUM_PE) : rc;
}

int cgra_reset_datapath(cgra_t *dev)
{
    if (dev == NULL)
        return CGRA_ERR_ARG;
    if (dev->desynced) return CGRA_ERR_DESYNC;
    dev->stats.transactions++;
    uint8_t cmd = CMD_RST;
    int rc = write_exact(dev, &cmd, 1);
    if (rc != CGRA_OK)
        return uncertain(dev, rc);
    rc = expect_ack(dev);
    return rc == CGRA_OK ? rc : uncertain(dev, rc);
}

/* -------------------------------------------------- fused execute (v3) */

static int popcount16(uint16_t m)
{
    int n = 0;
    while (m != 0) { n += m & 1; m = (uint16_t)(m >> 1); }
    return n;
}

int cgra_has_exec(cgra_t *dev)
{
    cgra_info_t info;
    return cgra_get_info(dev, &info) == CGRA_OK && info.version >= 3;
}

int cgra_exec_n(cgra_t *dev, uint8_t steps, unsigned flags, uint16_t tap_mask,
                const int16_t *west, size_t nwest,
                const int16_t *north, size_t nnorth,
                int16_t *taps, size_t ntaps)
{
    if (dev == NULL)
        return CGRA_ERR_ARG;
    if ((size_t)popcount16(tap_mask) != ntaps || (ntaps > 0 && taps == NULL))
        return CGRA_ERR_ARG;
    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;
    if (g.version < 3) return CGRA_ERR_PROTO;
    if ((west ? nwest != g.rows : nwest != 0) ||
        (north ? nnorth != g.cols : nnorth != 0) ||
        (flags & ~CGRA_EXEC_RESET) != 0 ||
        ((uint32_t)tap_mask >> (g.rows * g.cols)) != 0) return CGRA_ERR_ARG;
    size_t req_len = 1 + EXEC_HDR + 2 * ((size_t)g.rows + g.cols) + 1;

    uint8_t req[EXEC_REQ_MAX];
    req[0] = CMD_EXEC;
    req[1] = steps;
    req[2] = (uint8_t)(flags & 0xFFu);
    req[3] = (uint8_t)(tap_mask & 0xFFu);
    req[4] = (uint8_t)(tap_mask >> 8);
    for (int i = 0; i < g.rows; i++) {
        uint16_t v = west != NULL ? (uint16_t)west[i] : 0u;
        req[1 + EXEC_HDR + 2 * i + 0] = (uint8_t)(v & 0xFFu);
        req[1 + EXEC_HDR + 2 * i + 1] = (uint8_t)(v >> 8);
    }
    for (int i = 0; i < g.cols; i++) {
        uint16_t v = north != NULL ? (uint16_t)north[i] : 0u;
        int j = g.rows + i;
        req[1 + EXEC_HDR + 2 * j + 0] = (uint8_t)(v & 0xFFu);
        req[1 + EXEC_HDR + 2 * j + 1] = (uint8_t)(v >> 8);
    }
    req[req_len - 1] = checksum(req + 1, req_len - 2);

    uint8_t rsp[2 * CGRA_MAX_PE + 2];   /* taps + data checksum + status */
    size_t  dlen = 2 * ntaps;

    /* Only a complete, checksum-valid NACK may be retried. RESET cannot
     * repair an unknown parser boundary, so a timeout always stops the session. */
    unsigned attempts = (flags & CGRA_EXEC_RESET) ? dev->retries : 0;
    unsigned att = 0;
    dev->stats.transactions++;
    for (;;) {
        if (att > 0) dev->stats.retries++;
        rc = write_exact(dev, req, req_len);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        rc = read_exact(dev, rsp, dlen + 2);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        if (checksum(rsp, dlen) != rsp[dlen])
            return uncertain(dev, CGRA_ERR_PROTO);
        if (rsp[dlen + 1] == RSP_ACK) {
            for (size_t i = 0; i < ntaps; i++)
                taps[i] = (int16_t)((uint16_t)rsp[2 * i]
                                    | ((uint16_t)rsp[2 * i + 1] << 8));
            return CGRA_OK;
        }
        if (rsp[dlen + 1] != RSP_NACK)
            return uncertain(dev, CGRA_ERR_PROTO);
        if (att == attempts) return CGRA_ERR_NACK;
        ++att;
    }
}

int cgra_exec(cgra_t *dev, uint8_t steps, unsigned flags, uint16_t tap_mask,
              const int16_t west[CGRA_ROWS], const int16_t north[CGRA_COLS],
              int16_t *taps, size_t ntaps)
{
    int rc = require_default_geometry(dev);
    return rc == CGRA_OK
        ? cgra_exec_n(dev, steps, flags, tap_mask, west, west ? CGRA_ROWS : 0,
                      north, north ? CGRA_COLS : 0, taps, ntaps)
        : rc;
}

int cgra_apply(cgra_t *dev, const uint32_t cfg[CGRA_NUM_PE],
               const int16_t west[CGRA_ROWS], const int16_t north[CGRA_COLS],
               uint8_t steps, int16_t regs[CGRA_NUM_PE])
{
    int rc = require_default_geometry(dev);
    if (rc != CGRA_OK) return rc;
    if (cfg != NULL) {
        rc = cgra_configure(dev, cfg);
        if (rc != CGRA_OK) return rc;
    }
    if (west != NULL || north != NULL) {
        /* v3: inputs, run and read-back are one round trip instead of three. */
        if (cgra_has_exec(dev))
            return cgra_exec(dev, steps, 0u, regs != NULL ? 0xFFFFu : 0u,
                             west, north, regs, regs != NULL ? CGRA_NUM_PE : 0);
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
    uint32_t cfg[CGRA_MAX_PE];
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
