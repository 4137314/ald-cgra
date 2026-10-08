/*
 * cgra.c — serial transport and protocol primitives.
 * See hw/rtl/cgra_ctrl.vhd for the device-side protocol FSM.
 */

#define _DEFAULT_SOURCE

#include "cgra.h"
#include "bitvec.h"
#include "emu.h"
#include "block_wire.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
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
/* For positive R,C with RC <= MAX_PE, R+C <= MAX_PE+1. */
#define MAX_EDGE_WORDS (CGRA_MAX_PE + 1)
#define CFG_REQ_MAX (2 + 4 * CGRA_MAX_PE)

struct cgra_plan {
    cgra_plan_spec_t spec;
    size_t cfg_len;
    uint8_t cfg_packet[CFG_REQ_MAX];
};

struct cgra {
    int          fd;           /* -1 for the sim: emulator backend */
    unsigned     transaction_ms;
    int64_t      transaction_deadline; /* -1: no whole-transaction budget */
    int          exclusive;
    int block_mode;
    uint32_t block_session, block_next;
    size_t block_loaded[2];
    int64_t block_reply_deadline;
    cgra_diagnostic_t diagnostic, active;
    unsigned     timeout_ms;
    unsigned     retries;      /* retransmissions on complete NACK */
    int          desynced;     /* no further commands until close */
    cgra_emu_t  *emu;          /* non-NULL for the sim: backend    */
    int          proto_ver;    /* cached ID version, -1 = not asked yet */
    cgra_info_t  info;
    cgra_stats_t stats;
    int profiling;
    cgra_profile_t profile;
    size_t resident_len;       /* 0 unless explicitly bound by plan API */
    uint8_t resident_cfg[CFG_REQ_MAX];
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
                     int *legacy, int *flaky, int *blocks)
{
    *rows = CGRA_ROWS; *cols = CGRA_COLS;
    *legacy = 0; *flaky = 0; *blocks = 0;
    if (strcmp(device, "sim") == 0) return 1;
    char spec[64];
    if (strlen(device + 4) >= sizeof(spec)) return 0;
    strcpy(spec, device + 4);
    char *save = NULL;
    int geometry_seen = 0;
    for (char *token = strtok_r(spec, ":", &save); token;
         token = strtok_r(NULL, ":", &save)) {
        if (strcmp(token, "blocks") == 0) { *blocks = 1; continue; }
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
    return cgra_open_ex(device, baud, 0);
}

static void close_port(int fd, int exclusive)
{
#ifdef TIOCNXCL
    if (exclusive) (void)ioctl(fd, TIOCNXCL);
#else
    (void)exclusive;
#endif
    close(fd);
}

cgra_t *cgra_open_ex(const char *device, unsigned baud, unsigned flags)
{
    if (device == NULL || (flags & ~CGRA_OPEN_EXCLUSIVE)) {
        errno = EINVAL;
        return NULL;
    }

    /* In-process emulator: device string "sim" or "sim:...". No real port,
     * baud is irrelevant. Lets the whole stack run without an FPGA. */
    if (strcmp(device, "sim") == 0 || strncmp(device, "sim:", 4) == 0) {
        if (flags) { errno = EINVAL; return NULL; }
        unsigned rows, cols;
        int legacy, flaky, blocks;
        if (!parse_sim(device, &rows, &cols, &legacy, &flaky, &blocks)) { errno = EINVAL; return NULL; }
        cgra_t *dev = calloc(1, sizeof(*dev));
        if (dev == NULL)
            return NULL;
        dev->emu = emu_new_geometry(rows, cols);
        if (dev->emu == NULL || (blocks && emu_enable_blocks(dev->emu))) {
            emu_free(dev->emu);
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

    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    int exclusive = (flags & CGRA_OPEN_EXCLUSIVE) != 0;
    if (exclusive) {
#ifdef TIOCEXCL
        if (ioctl(fd, TIOCEXCL) != 0) {
            int saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return NULL;
        }
#else
        close(fd);
        errno = ENOTSUP;
        return NULL;
#endif
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        int saved_errno = errno;
        close_port(fd, exclusive);
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
        close_port(fd, exclusive);
        errno = saved_errno;
        return NULL;
    }

    cgra_t *dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        close_port(fd, exclusive);
        errno = ENOMEM;
        return NULL;
    }
    dev->fd = fd;
    dev->exclusive = exclusive;
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
            close_port(dev->fd, dev->exclusive);
        free(dev);
    }
}

void cgra_set_timeout(cgra_t *dev, unsigned ms)
{
    if (dev != NULL)
        dev->timeout_ms = ms;
}

void cgra_set_transaction_timeout(cgra_t *dev, unsigned ms)
{
    if (dev != NULL) dev->transaction_ms = ms;
}

void cgra_get_diagnostic(const cgra_t *dev, cgra_diagnostic_t *diagnostic)
{
    if (dev != NULL && diagnostic != NULL) *diagnostic = dev->diagnostic;
}

void cgra_clear_diagnostic(cgra_t *dev)
{
    if (dev != NULL) memset(&dev->diagnostic, 0, sizeof(dev->diagnostic));
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

void cgra_set_profiling(cgra_t *dev, int enabled)
{
    if (!dev) return;
    dev->profiling = enabled != 0;
    if (enabled) dev->profile = (cgra_profile_t){.valid = 1};
}

void cgra_get_profile(cgra_t *dev, cgra_profile_t *profile)
{
    if (dev && profile) *profile = dev->profile;
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
    case CGRA_ERR_UNSUPPORTED: return "operation unavailable in this protocol mode";
    case CGRA_ERR_STALE:    return "plan configuration is not bound; bind explicitly";
    default:               return "unknown error";
    }
}

static int failed(cgra_t *dev, int rc, enum cgra_error_phase phase, int cause)
{
    dev->diagnostic = dev->active;
    dev->diagnostic.code = rc;
    dev->diagnostic.phase = phase;
    dev->diagnostic.system_errno = cause;
    return rc;
}

static int monotonic_ns(int64_t *value)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    *value = (int64_t)now.tv_sec * INT64_C(1000000000) + now.tv_nsec;
    return 0;
}

static int begin_transaction(cgra_t *dev, uint8_t command)
{
    dev->active = (cgra_diagnostic_t){.command = command, .attempt = 1};
    if (dev->block_mode && command != 0x7e)
        return failed(dev, CGRA_ERR_UNSUPPORTED, CGRA_PHASE_PREPARE, 0);
    dev->block_reply_deadline = -1;
    dev->resident_len = 0;
    dev->transaction_deadline = -1;
    if (dev->transaction_ms && dev->emu == NULL) {
        int64_t now;
        if (monotonic_ns(&now) != 0)
            return failed(dev, CGRA_ERR_IO, CGRA_PHASE_PREPARE, errno);
        dev->transaction_deadline = now + (int64_t)dev->transaction_ms * 1000000;
    }
    dev->stats.transactions++;
    if (dev->profiling && command == CMD_CFG) dev->profile.configuration_transactions++;
    return CGRA_OK;
}

/* A transfer failure leaves parser boundary and datapath state uncertain.
 * Never send an automatic RST or retry after such a failure. */
static int uncertain(cgra_t *dev, int rc)
{
    if (rc == CGRA_ERR_PROTO || rc == CGRA_ERR_NACK)
        failed(dev, rc, CGRA_PHASE_VALIDATE, 0);
    dev->desynced = 1;
    dev->proto_ver = -1;
    memset(&dev->info, 0, sizeof(dev->info));
    return rc;
}

/* -1 means an unlimited write wait. Reply timeout 0 still permits its first
 * immediate readiness check, as in the original API. All descriptors are
 * nonblocking: poll readiness cannot lead to an unbounded read/write race. */
static int wait_ready(cgra_t *dev, short events, int64_t deadline,
                       int immediate, enum cgra_error_phase phase)
{
    for (;;) {
        int timeout = -1;
        if (deadline >= 0) {
            int64_t now;
            if (monotonic_ns(&now) != 0)
                return failed(dev, CGRA_ERR_IO, phase, errno);
            int64_t remaining = deadline - now;
            if (remaining <= 0 && !immediate)
                return failed(dev, CGRA_ERR_TIMEOUT, phase, ETIMEDOUT);
            int64_t ms = remaining > 0 ? (remaining + 999999) / 1000000 : 0;
            timeout = ms > INT_MAX ? INT_MAX : (int)ms;
        }
        int allow_ready_now = immediate;
        immediate = 0;
        struct pollfd pfd = {.fd = dev->fd, .events = events};
        int rc = poll(&pfd, 1, timeout);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return failed(dev, CGRA_ERR_IO, phase, errno);
        }
        if (rc == 0) continue;
        if (pfd.revents & events) {
            if (deadline >= 0 && !allow_ready_now) {
                int64_t now;
                if (monotonic_ns(&now) != 0)
                    return failed(dev, CGRA_ERR_IO, phase, errno);
                if (now >= deadline)
                    return failed(dev, CGRA_ERR_TIMEOUT, phase, ETIMEDOUT);
            }
            return CGRA_OK;
        }
        return failed(dev, CGRA_ERR_IO, phase, (pfd.revents & POLLNVAL) ? EBADF : EIO);
    }
}

static int write_exact_impl(cgra_t *dev, const uint8_t *buf, size_t n)
{
    if (dev->emu != NULL) {
        emu_feed(dev->emu, buf, n);
        dev->stats.tx_bytes += n;
        dev->active.tx_bytes += n;
        return CGRA_OK;
    }
    size_t sent = 0;
    while (sent < n) {
        int rc = wait_ready(dev, POLLOUT, dev->transaction_deadline, 0, CGRA_PHASE_WRITE);
        if (rc != CGRA_OK) return rc;
        ssize_t k = write(dev->fd, buf + sent, n - sent);
        if (k < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return failed(dev, CGRA_ERR_IO, CGRA_PHASE_WRITE, errno);
        }
        if (k == 0) return failed(dev, CGRA_ERR_IO, CGRA_PHASE_WRITE, EIO);
        sent += (size_t)k;
        dev->stats.tx_bytes += (size_t)k;
        dev->active.tx_bytes += (size_t)k;
    }
    return CGRA_OK;
}

static int read_exact_impl(cgra_t *dev, uint8_t *buf, size_t n)
{
    if (dev->emu != NULL) {
        size_t got = emu_drain(dev->emu, buf, n);
        dev->stats.rx_bytes += got;
        dev->active.rx_bytes += got;
        return got == n ? CGRA_OK : failed(dev, CGRA_ERR_TIMEOUT, CGRA_PHASE_READ, 0);
    }
    int64_t now;
    if (monotonic_ns(&now) != 0)
        return failed(dev, CGRA_ERR_IO, CGRA_PHASE_READ, errno);
    int64_t deadline = now + (int64_t)dev->timeout_ms * 1000000;
    int immediate = dev->timeout_ms == 0;
    if (dev->block_reply_deadline >= 0 && dev->block_reply_deadline < deadline)
        deadline = dev->block_reply_deadline;
    if (dev->transaction_deadline >= 0 && dev->transaction_deadline <= deadline) {
        deadline = dev->transaction_deadline;
        immediate = 0;
    }
    size_t got = 0;
    while (got < n) {
        int rc = wait_ready(dev, POLLIN, deadline, immediate, CGRA_PHASE_READ);
        immediate = 0;
        if (rc != CGRA_OK) return rc;
        ssize_t k = read(dev->fd, buf + got, n - got);
        if (k < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return failed(dev, CGRA_ERR_IO, CGRA_PHASE_READ, errno);
        }
        if (k == 0) return failed(dev, CGRA_ERR_IO, CGRA_PHASE_READ, EIO);
        got += (size_t)k;
        dev->stats.rx_bytes += (size_t)k;
        dev->active.rx_bytes += (size_t)k;
    }
    return CGRA_OK;
}

static int64_t profile_start(cgra_t *dev)
{
    int saved = errno;
    int64_t start = 0;
    if (monotonic_ns(&start)) dev->profile.valid = 0;
    errno = saved;
    return start;
}

static void profile_end(cgra_t *dev, int64_t start)
{
    int saved = errno;
    int64_t end;
    if (monotonic_ns(&end) || end < start) dev->profile.valid = 0;
    else if (dev->profile.valid) {
        uint64_t *total = dev->active.command == CMD_CFG
                        ? &dev->profile.configuration_io_ns : &dev->profile.execution_io_ns;
        *total += (uint64_t)(end - start);
    }
    errno = saved;
}

static int write_exact(cgra_t *dev, const uint8_t *buf, size_t n)
{
    if (!dev->profiling) return write_exact_impl(dev, buf, n);
    int64_t start = profile_start(dev);
    int rc = write_exact_impl(dev, buf, n);
    profile_end(dev, start);
    return rc;
}

static int read_exact(cgra_t *dev, uint8_t *buf, size_t n)
{
    if (!dev->profiling) return read_exact_impl(dev, buf, n);
    int64_t start = profile_start(dev);
    int rc = read_exact_impl(dev, buf, n);
    profile_end(dev, start);
    return rc;
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
    int rc = begin_transaction(dev, buf[0]);
    if (rc != CGRA_OK) return rc;
    unsigned att = 0;
    for (;;) {
        if (att > 0) dev->stats.retries++;
        dev->active.attempt = (uint64_t)att + 1;
        rc = write_exact(dev, buf, n);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        rc = expect_ack(dev);
        if (rc == CGRA_OK) return rc;
        if (rc != CGRA_ERR_NACK) return uncertain(dev, rc);
        if (att == dev->retries) return failed(dev, rc, CGRA_PHASE_VALIDATE, 0);
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
    int rc = begin_transaction(dev, CMD_ID);
    if (rc != CGRA_OK) return rc;
    uint8_t cmd = CMD_ID;
    uint8_t rsp[5];
    rc = write_exact(dev, &cmd, 1);
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
    if (g->rows == 0 || g->rows > CGRA_MAX_EDGE ||
        g->cols == 0 || g->cols > CGRA_MAX_EDGE || g->data_w != CGRA_DATA_W ||
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
    if (dev == NULL || cfg == NULL || ncfg > CGRA_MAX_PE) return CGRA_ERR_ARG;
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
    if (dev == NULL || west == NULL || north == NULL ||
        nwest > CGRA_MAX_EDGE || nnorth > CGRA_MAX_EDGE) return CGRA_ERR_ARG;
    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;
    if (nwest != g.rows || nnorth != g.cols) return CGRA_ERR_ARG;
    size_t edges = nwest + nnorth;
    if (edges > MAX_EDGE_WORDS) return CGRA_ERR_GEOMETRY;
    uint8_t buf[1 + 4 * CGRA_MAX_EDGE + 1];
    size_t len = 1 + 2 * edges + 1;
    buf[0] = CMD_WR;
    for (size_t i = 0; i < edges; ++i) {
        uint16_t value = (uint16_t)(i < nwest ? west[i] : north[i - nwest]);
        size_t offset = 1 + 2 * i;
        buf[offset] = (uint8_t)value;
        buf[offset + 1] = (uint8_t)(value >> 8);
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
    int rc = begin_transaction(dev, CMD_RUN);
    if (rc != CGRA_OK) return rc;
    if (dev->profiling) dev->profile.requested_steps += steps;
    uint8_t buf[2] = { CMD_RUN, steps };
    rc = write_exact(dev, buf, sizeof(buf));
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
    rc = begin_transaction(dev, CMD_RD);
    if (rc != CGRA_OK) return rc;
    rc = write_exact(dev, &cmd, 1);
    if (rc != CGRA_OK) return uncertain(dev, rc);
    rc = read_exact(dev, buf, 2 * count + 1);
    if (rc != CGRA_OK) return uncertain(dev, rc);
    if (checksum(buf, 2 * count) != buf[2 * count])
        return uncertain(dev, CGRA_ERR_PROTO);
    for (size_t i = 0; i < count; i++)
        regs[i] = cgra_i16_from_bits((uint16_t)((uint16_t)buf[2 * i] | ((uint16_t)buf[2 * i + 1] << 8)));
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
    int rc = begin_transaction(dev, CMD_RST);
    if (rc != CGRA_OK) return rc;
    uint8_t cmd = CMD_RST;
    rc = write_exact(dev, &cmd, 1);
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
    /* Keep the serialization bound visible locally, including to compilers
     * that do not carry get_info's geometry invariant across calls. */
    size_t edges = (size_t)g.rows + g.cols;
    if (g.rows == 0 || g.cols == 0 || g.rows > CGRA_MAX_EDGE ||
        g.cols > CGRA_MAX_EDGE || edges > MAX_EDGE_WORDS)
        return CGRA_ERR_GEOMETRY;
    for (size_t i = 0; i < edges; i++) {
        uint16_t v = i < g.rows ? (west ? (uint16_t)west[i] : 0u)
                               : (north ? (uint16_t)north[i - g.rows] : 0u);
        req[1 + EXEC_HDR + 2 * i] = (uint8_t)v;
        req[2 + EXEC_HDR + 2 * i] = (uint8_t)(v >> 8);
    }
    req[req_len - 1] = checksum(req + 1, req_len - 2);

    uint8_t rsp[2 * CGRA_MAX_PE + 2];   /* taps + data checksum + status */
    size_t  dlen = 2 * ntaps;

    /* Only a complete, checksum-valid NACK may be retried. RESET cannot
     * repair an unknown parser boundary, so a timeout always stops the session. */
    unsigned attempts = (flags & CGRA_EXEC_RESET) ? dev->retries : 0;
    unsigned att = 0;
    rc = begin_transaction(dev, CMD_EXEC);
    if (rc != CGRA_OK) return rc;
    if (dev->profiling) dev->profile.requested_steps += steps;
    for (;;) {
        if (att > 0) dev->stats.retries++;
        dev->active.attempt = (uint64_t)att + 1;
        rc = write_exact(dev, req, req_len);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        rc = read_exact(dev, rsp, dlen + 2);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        if (checksum(rsp, dlen) != rsp[dlen])
            return uncertain(dev, CGRA_ERR_PROTO);
        if (rsp[dlen + 1] == RSP_ACK) {
            for (size_t i = 0; i < ntaps; i++)
                taps[i] = cgra_i16_from_bits((uint16_t)((uint16_t)rsp[2 * i]
                                    | ((uint16_t)rsp[2 * i + 1] << 8)));
            return CGRA_OK;
        }
        if (rsp[dlen + 1] != RSP_NACK)
            return uncertain(dev, CGRA_ERR_PROTO);
        if (att == attempts) return failed(dev, CGRA_ERR_NACK, CGRA_PHASE_VALIDATE, 0);
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

int cgra_plan_create(const cgra_plan_spec_t *spec, const uint32_t *cfg,
                     size_t ncfg, cgra_plan_t **plan)
{
    if (plan == NULL) return CGRA_ERR_ARG;
    *plan = NULL;
    if (spec == NULL || cfg == NULL || ncfg > CGRA_MAX_PE ||
        spec->rows == 0 || spec->cols == 0 ||
        spec->rows > CGRA_MAX_EDGE || spec->cols > CGRA_MAX_EDGE ||
        spec->rows * spec->cols > CGRA_MAX_PE ||
        ncfg != spec->rows * spec->cols ||
        (spec->flags & ~CGRA_EXEC_RESET) ||
        ((uint32_t)spec->tap_mask >> ncfg)) return CGRA_ERR_ARG;
    cgra_plan_t *p = calloc(1, sizeof(*p));
    if (p == NULL) return CGRA_ERR_NOMEM;
    p->spec = *spec;
    p->cfg_len = 2 + 4 * ncfg;
    p->cfg_packet[0] = CMD_CFG;
    for (size_t i = 0; i < ncfg; ++i)
        for (unsigned j = 0; j < 4; ++j)
            p->cfg_packet[1 + 4*i + j] = (uint8_t)(cfg[i] >> (8*j));
    p->cfg_packet[p->cfg_len - 1] = checksum(p->cfg_packet + 1, p->cfg_len - 2);
    *plan = p;
    return CGRA_OK;
}

void cgra_plan_free(cgra_plan_t *plan)
{
    free(plan);
}

void cgra_plan_invalidate(cgra_t *dev)
{
    if (dev) dev->resident_len = 0;
}

static void bind_copy(cgra_t *dev, const cgra_plan_t *plan)
{
    memcpy(dev->resident_cfg, plan->cfg_packet, plan->cfg_len);
    dev->resident_len = plan->cfg_len;
}

int cgra_plan_bind(cgra_t *dev, const cgra_plan_t *plan)
{
    cgra_plan_invalidate(dev);
    if (dev == NULL || plan == NULL) return CGRA_ERR_ARG;
    cgra_info_t g;
    int rc = cgra_get_info(dev, &g);
    if (rc != CGRA_OK) return rc;
    if (g.rows != plan->spec.rows || g.cols != plan->spec.cols)
        return CGRA_ERR_GEOMETRY;
    rc = send_expect_ack(dev, plan->cfg_packet, plan->cfg_len);
    if (rc == CGRA_OK) bind_copy(dev, plan);
    return rc;
}

int cgra_plan_execute_n(cgra_t *dev, const cgra_plan_t *plan,
                       const int16_t *west, size_t nwest,
                       const int16_t *north, size_t nnorth,
                       int16_t *taps, size_t ntaps)
{
    if (dev == NULL || plan == NULL) return CGRA_ERR_ARG;
    const cgra_plan_spec_t *p = &plan->spec;
    if ((west ? nwest != p->rows : nwest != 0) ||
        (north ? nnorth != p->cols : nnorth != 0) ||
        ntaps != (size_t)popcount16(p->tap_mask) || (ntaps && taps == NULL))
        return CGRA_ERR_ARG;
    if (dev->desynced) return CGRA_ERR_DESYNC;
    if (dev->resident_len != plan->cfg_len ||
        dev->info.rows != p->rows || dev->info.cols != p->cols ||
        memcmp(dev->resident_cfg, plan->cfg_packet, plan->cfg_len) != 0)
        return CGRA_ERR_STALE;
    int rc;
    if (dev->info.version >= 3) {
        rc = cgra_exec_n(dev, p->steps, p->flags, p->tap_mask,
                         west, nwest, north, nnorth, taps, ntaps);
    } else {
        const int16_t zeros[CGRA_MAX_EDGE] = {0};
        int16_t regs[CGRA_MAX_PE];
        rc = cgra_write_inputs_n(dev, west ? west : zeros, p->rows,
                                 north ? north : zeros, p->cols);
        if (rc == CGRA_OK && (p->flags & CGRA_EXEC_RESET)) rc = cgra_reset_datapath(dev);
        if (rc == CGRA_OK) rc = cgra_run(dev, p->steps);
        if (rc == CGRA_OK && ntaps) {
            rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE);
            if (rc == CGRA_OK) {
                size_t j = 0;
                for (size_t i = 0; i < p->rows * p->cols; ++i)
                    if (p->tap_mask & (1u << i)) taps[j++] = regs[i];
            }
        }
    }
    if (rc == CGRA_OK) bind_copy(dev, plan);
    else cgra_plan_invalidate(dev);
    return rc;
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

/* -------------------------------------------------- experimental blocks */
static int block_reply_clock(cgra_t *dev)
{
    if (dev->emu) return CGRA_OK;
    int64_t now;
    if (monotonic_ns(&now)) return failed(dev, CGRA_ERR_IO, CGRA_PHASE_READ, errno);
    dev->block_reply_deadline = now + (int64_t)dev->timeout_ms * 1000000;
    return CGRA_OK;
}

static int block_exchange(cgra_t *dev, const block_frame *q, block_frame *r)
{
    uint8_t bytes[BLOCK_WIRE];
    size_t n = block_encode(q, bytes, sizeof bytes);
    if (!n) return CGRA_ERR_ARG;
    int rc = begin_transaction(dev, BLOCK_FLAG);
    if (rc != CGRA_OK) return rc;
    rc = write_exact(dev, bytes, n);
    if (rc != CGRA_OK) return uncertain(dev, rc);
    rc = block_reply_clock(dev);
    if (rc != CGRA_OK) return uncertain(dev, rc);
    block_decoder decoder = {0};
    /* At most 19 raw reply bytes (RESULT) -> 40 escaped bytes including flags.
     * All byte reads share ONE reply deadline, independently of writes. */
    for (size_t i = 0; i < 40; ++i) {
        uint8_t b;
        rc = read_exact(dev, &b, 1);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        if (!i && b != BLOCK_FLAG) return uncertain(dev, CGRA_ERR_PROTO);
        int valid = block_decode(&decoder, b, r);
        if (valid < 0) return uncertain(dev, CGRA_ERR_PROTO);
        if (!valid) continue;
        if (r->opcode != (uint8_t)(q->opcode | 0x80u) || r->session != q->session ||
            r->sequence != q->sequence || !r->length || r->payload[0] > BLOCK_NOT_READY)
            return uncertain(dev, CGRA_ERR_PROTO);
        unsigned status = r->payload[0];
        size_t expected = status != BLOCK_OK ? 1 : q->opcode == BLOCK_HELLO ? 6 :
                          q->opcode == BLOCK_RESULT ? 7 : 1;
        if (r->length != expected) return uncertain(dev, CGRA_ERR_PROTO);
        if (status == BLOCK_SESSION || status == BLOCK_SEQUENCE || status == BLOCK_CONFLICT)
            return uncertain(dev, CGRA_ERR_NACK);
        dev->block_next = (uint32_t)q->sequence + 1u;
        if (status != BLOCK_OK) {
            return failed(dev, CGRA_ERR_NACK, CGRA_PHASE_VALIDATE, 0);
        }
        return CGRA_OK;
    }
    return uncertain(dev, CGRA_ERR_PROTO);
}

int cgra_block_begin(cgra_t *dev, uint32_t nonce)
{
    if (!dev || !nonce || (dev->block_session && dev->block_session == nonce)) return CGRA_ERR_ARG;
    if (dev->desynced) return CGRA_ERR_DESYNC;
    if (!dev->block_mode) {
        const uint8_t command = 0x08;
        uint8_t cap[5];
        int rc = begin_transaction(dev, command);
        if (rc != CGRA_OK) return rc;
        rc = write_exact(dev, &command, 1);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        rc = block_reply_clock(dev);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        rc = read_exact(dev, cap, 1);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        if (cap[0] == RSP_NACK) return failed(dev, CGRA_ERR_UNSUPPORTED, CGRA_PHASE_VALIDATE, 0);
        if (cap[0] != 0xcb) return uncertain(dev, CGRA_ERR_PROTO);
        rc = read_exact(dev, cap+1, 4);
        if (rc != CGRA_OK) return uncertain(dev, rc);
        const uint8_t expected[] = {0xcb, BLOCK_VERSION, BLOCK_CHUNK, 0, 4};
        if (memcmp(cap, expected, sizeof cap)) return uncertain(dev, CGRA_ERR_PROTO);
        dev->block_mode = 1;
    }
    block_frame q = {.opcode = BLOCK_HELLO, .session = nonce}, r;
    int rc = block_exchange(dev, &q, &r);
    if (rc != CGRA_OK) return rc;
    if (block_get16(r.payload+1) != BLOCK_WORDS || r.payload[3] != BLOCK_CHUNK ||
        block_get16(r.payload+4) != 1) return uncertain(dev, CGRA_ERR_PROTO);
    dev->block_session = nonce;
    dev->block_loaded[0] = dev->block_loaded[1] = 0;
    return CGRA_OK;
}

static int block_ready(cgra_t *dev)
{
    if (!dev) return CGRA_ERR_ARG;
    if (dev->desynced) return CGRA_ERR_DESYNC;
    if (!dev->block_session || dev->block_next > UINT16_MAX) return CGRA_ERR_ARG;
    return CGRA_OK;
}

int cgra_block_upload(cgra_t *dev, unsigned bank, const int16_t *data, size_t n)
{
    if (bank > 1 || !data || !n || n > BLOCK_WORDS) return CGRA_ERR_ARG;
    int rc = block_ready(dev);
    if (rc != CGRA_OK) return rc;
    size_t frames = (n + BLOCK_CHUNK-1) / BLOCK_CHUNK;
    if (frames > UINT16_MAX+1u - dev->block_next) return CGRA_ERR_ARG;
    for (size_t offset = 0; offset < n; offset += BLOCK_CHUNK) {
        size_t count = n-offset < BLOCK_CHUNK ? n-offset : BLOCK_CHUNK;
        block_frame q = {.opcode = BLOCK_UPLOAD, .session = dev->block_session,
                         .sequence = (uint16_t)dev->block_next, .length = 4+2*count}, r;
        q.payload[0] = (uint8_t)bank;
        block_put16(q.payload+1, (uint16_t)offset); q.payload[3] = (uint8_t)count;
        for (size_t i = 0; i < count; ++i) block_put16(q.payload+4+2*i, (uint16_t)data[offset+i]);
        rc = block_exchange(dev, &q, &r);
        if (rc != CGRA_OK) return rc;
        dev->block_loaded[bank] = offset+count;
    }
    return CGRA_OK;
}

int cgra_block_result(cgra_t *dev, int16_t *result, uint32_t *executions)
{
    if (!result) return CGRA_ERR_ARG;
    int rc = block_ready(dev);
    if (rc != CGRA_OK) return rc;
    block_frame q = {.opcode = BLOCK_RESULT, .session = dev->block_session,
                     .sequence = (uint16_t)dev->block_next}, r;
    rc = block_exchange(dev, &q, &r);
    if (rc != CGRA_OK) return rc;
    *result = cgra_i16_from_bits(block_get16(r.payload+1));
    if (executions) *executions = block_get32(r.payload+3);
    return CGRA_OK;
}

int cgra_block_dot(cgra_t *dev, size_t n, int16_t *result, uint32_t *executions)
{
    if (!result || n > BLOCK_WORDS) return CGRA_ERR_ARG;
    int rc = block_ready(dev);
    if (rc != CGRA_OK) return rc;
    if (n > dev->block_loaded[0] || n > dev->block_loaded[1] || dev->block_next >= UINT16_MAX)
        return CGRA_ERR_ARG; /* reserve both DOT and RESULT before effects */
    block_frame q = {.opcode = BLOCK_DOT, .session = dev->block_session,
                     .sequence = (uint16_t)dev->block_next, .length = 2}, r;
    block_put16(q.payload, (uint16_t)n);
    rc = block_exchange(dev, &q, &r);
    return rc == CGRA_OK ? cgra_block_result(dev, result, executions) : rc;
}
