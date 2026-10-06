/* Real POSIX transport regressions, using only local pseudo-terminals. */
#define _XOPEN_SOURCE 700
#include "cgra.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/select.h>   /* FD_SETSIZE: regression threshold, not transport I/O */
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int checks;
static int fragment_writes, inject_interrupt, inject_again;
ssize_t __real_write(int fd, const void *buf, size_t n);
ssize_t __wrap_write(int fd, const void *buf, size_t n);
ssize_t __wrap_write(int fd, const void *buf, size_t n)
{
    if (inject_interrupt) { inject_interrupt = 0; errno = EINTR; return -1; }
    if (inject_again) { inject_again = 0; errno = EAGAIN; return -1; }
    return __real_write(fd, buf, fragment_writes && n > 1 ? 1 : n);
}
#define CHECK(cond, message) do { \
    int passed = (cond); \
    printf("%s transport %d - %s\n", passed ? "ok" : "not ok", ++checks, message); \
    assert(passed); \
} while (0)

static cgra_t *open_peer(int *master)
{
    *master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(*master >= 0 && grantpt(*master) == 0 && unlockpt(*master) == 0, "create PTY");
    cgra_t *dev = cgra_open(ptsname(*master), 115200);
    CHECK(dev != NULL, "open serial library on PTY");
    return dev;
}

static void pause_ms(long ms)
{
    struct timespec delay = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) { }
}

static void test_silent_reply(void)
{
    int master;
    cgra_t *dev = open_peer(&master);
    cgra_set_timeout(dev, 20);
    CHECK(cgra_identify(dev, NULL) == CGRA_ERR_TIMEOUT, "silent peer times out");
    cgra_stats_t stats;
    cgra_get_stats(dev, &stats);
    CHECK(stats.tx_bytes == 1 && stats.rx_bytes == 0, "silent peer contributes no received bytes");
    cgra_close(dev);
    close(master);
}

static void test_fragmented_reply(void)
{
    int master;
    cgra_t *dev = open_peer(&master);
    cgra_set_timeout(dev, 50);
    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        alarm(5);
        cgra_close(dev);
        unsigned char command;
        if (read(master, &command, 1) != 1 || command != 1) _exit(1);
        const unsigned char reply[] = {0xCA, 3, 4, 4, 16};
        for (size_t i = 0; i < sizeof(reply); i++) {
            if (i) pause_ms(30);
            if (write(master, &reply[i], 1) != 1) _exit(1);
        }
        close(master);
        _exit(0);
    }
    CHECK(child >= 0, "fork delayed peer");
    CHECK(cgra_identify(dev, NULL) == CGRA_ERR_TIMEOUT,
          "fragments cannot restart the whole-reply deadline");
    cgra_stats_t stats;
    cgra_get_stats(dev, &stats);
    CHECK(stats.rx_bytes < 5, "only bytes read before timeout are counted");
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "delayed peer delivered the complete reply");
    cgra_close(dev);
    close(master);
}

static void test_failed_write(void)
{
    int master;
    cgra_t *dev = open_peer(&master);
    close(master);
    CHECK(cgra_identify(dev, NULL) == CGRA_ERR_IO, "disconnected PTY rejects write");
    cgra_stats_t stats;
    cgra_get_stats(dev, &stats);
    CHECK(stats.tx_bytes == 0 && stats.rx_bytes == 0, "failed transfer does not invent byte counts");
    cgra_close(dev);
}

static void test_uncertain_session(void)
{
    /* The peer consumes a request but may discard any suffix before delivery
     * to its controller. Check the wire: no implicit RST or retry may follow. */
    for (int scenario = 0; scenario < 7; ++scenario) {
        int master;
        cgra_t *dev = open_peer(&master);
        cgra_set_timeout(dev, 10);
        const unsigned char id[] = {0xCA, 3, 1, 1, 16};
        unsigned char wire[64];
        CHECK(write(master, id, sizeof(id)) == sizeof(id) &&
              cgra_identify(dev, NULL) == CGRA_OK &&
              read(master, wire, 1) == 1 && wire[0] == 1,
              "establish geometry before injecting reply failure");
        uint32_t cfg = 0;
        int16_t input = 0, output = 1234;
        size_t expected = 0;
        int rc;
        if (scenario >= 5) {
            /* A full response with a wrong checksum must not be accepted or
             * retried either (RD and EXEC respectively). */
            const unsigned char bad[] = {0, 0, 1, 0x79};
            size_t len = scenario == 5 ? 3 : 4;
            CHECK(write(master, bad, len) == (ssize_t)len, "inject malformed reply");
        }
        switch (scenario) {
        case 0: rc = cgra_run(dev, 1); expected = 2; break;
        case 1: rc = cgra_configure_n(dev, &cfg, 1); expected = 6; break;
        case 2: rc = cgra_write_inputs_n(dev, &input, 1, &input, 1); expected = 6; break;
        case 3: case 5:
            rc = cgra_read_regs_n(dev, &output, 1); expected = 1; break;
        default:
            rc = cgra_exec_n(dev, 1, CGRA_EXEC_RESET, 1,
                             NULL, 0, NULL, 0, &output, 1);
            expected = 10; break;
        }
        CHECK(rc == (scenario >= 5 ? CGRA_ERR_PROTO : CGRA_ERR_TIMEOUT),
              "failing command preserves its original error");
        cgra_stats_t before, after;
        cgra_get_stats(dev, &before);
        CHECK(before.tx_bytes == 1 + expected && before.retries == 0,
              "uncertain request is sent once with no recovery byte");
        CHECK(read(master, wire, sizeof(wire)) == (ssize_t)expected,
              "peer observes exactly the original request");
        CHECK(cgra_get_info(dev, NULL) == CGRA_ERR_DESYNC &&
              cgra_identify(dev, NULL) == CGRA_ERR_DESYNC &&
              cgra_reset_datapath(dev) == CGRA_ERR_DESYNC &&
              cgra_run(dev, 0) == CGRA_ERR_DESYNC &&
              cgra_configure_n(dev, &cfg, 1) == CGRA_ERR_DESYNC &&
              cgra_write_inputs_n(dev, &input, 1, &input, 1) == CGRA_ERR_DESYNC &&
              cgra_read_regs_n(dev, &output, 1) == CGRA_ERR_DESYNC &&
              cgra_exec_n(dev, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0) == CGRA_ERR_DESYNC,
              "all protocol entry points reject a desynchronized session");
        cgra_get_stats(dev, &after);
        struct pollfd pfd = {.fd = master, .events = POLLIN};
        CHECK(after.transactions == before.transactions &&
              after.tx_bytes == before.tx_bytes && after.rx_bytes == before.rx_bytes &&
              poll(&pfd, 1, 0) == 0 && output == 1234,
              "blocked calls leave wire, counters and output untouched");
        cgra_close(dev);
        close(master);
    }
}

static void test_high_descriptor(void)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur <= (rlim_t)FD_SETSIZE + 16) {
        puts("ok transport - high descriptor # SKIP process descriptor limit too low");
        return;
    }
    int held[FD_SETSIZE + 16];
    size_t count = 0;
    int reserved = 1;
    do {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) { reserved = 0; break; }
        held[count++] = fd;
    } while (held[count - 1] < FD_SETSIZE && count < sizeof(held) / sizeof(held[0]));
    CHECK(reserved && count > 0 && held[count - 1] >= FD_SETSIZE, "reserve descriptors through FD_SETSIZE");
    int master;
    cgra_t *dev = open_peer(&master);
    CHECK(master > FD_SETSIZE, "serial descriptor is above the select bitmap limit");
    cgra_set_timeout(dev, 1);
    CHECK(cgra_identify(dev, NULL) == CGRA_ERR_TIMEOUT, "high descriptor returns error instead of aborting");
    cgra_close(dev);
    close(master);
    for (size_t i = 0; i < count; i++) close(held[i]);
}

static void test_kernel_partial_output(void)
{
    for (int kernel = 0; kernel < 5; kernel++) {
        int master;
        cgra_t *dev = open_peer(&master);
        cgra_set_timeout(dev, 20);
        const unsigned char id[] = {0xCA, 3, 1, 1, 16};
        unsigned char wire[128];
        CHECK(write(master, id, sizeof(id)) == sizeof(id) &&
              cgra_get_info(dev, NULL) == CGRA_OK && read(master, wire, 1) == 1,
              "identify kernel fault-injection peer");
        /* Accept CFG and the first chunk, then corrupt the second reply.
         * Conv configures each nonzero column tile; dot has RST and no taps. */
        const unsigned char chunks[] = {0x79, 1, 0, 1, 0x79, 0, 0, 1, 0x79};
        const unsigned char conv[] = {0x79, 1, 0, 1, 0x79, 0x79, 0, 0, 1, 0x79};
        const unsigned char dot[] = {0x79, 0x79, 0, 0x79, 1, 0x79};
        const unsigned char *reply = kernel == 3 ? conv : kernel == 4 ? dot : chunks;
        size_t length = kernel == 3 ? sizeof(conv) : kernel == 4 ? sizeof(dot) : sizeof(chunks);
        CHECK(write(master, reply, length) == (ssize_t)length, "queue mid-kernel protocol error");
        const int16_t a[] = {1, 2, 3}, b[] = {0, 0, 0}, weight = 1;
        int16_t out[] = {999, 999, 999, 1234};
        int rc;
        switch (kernel) {
        case 0: rc = cgra_vec_add(dev, a, b, out, 3); break;
        case 1: rc = cgra_scan(dev, CGRA_OP_ADD, a, 3, out, 3); break;
        case 2: rc = cgra_matvec(dev, a, 3, 1, &weight, out, 3); break;
        case 3: rc = cgra_conv(dev, &weight, 1, a, 3, out, 3); break;
        default: rc = cgra_dot(dev, a, a, 3, out); break;
        }
        CHECK(rc == CGRA_ERR_PROTO, "kernel returns the original mid-execution error");
        CHECK(out[3] == 1234 && (kernel == 4 ? out[0] == 999 : out[0] == 1) &&
              out[1] == (kernel == 2 || kernel == 3 ? 0 : 999) &&
              out[2] == (kernel == 2 || kernel == 3 ? 0 : 999),
              "prefix/partial sums are explicit; dot and failed chunk remain unpublished");
        cgra_info_t info = {99, 99, 99, 99}, expected = info;
        cgra_stats_t before, after;
        cgra_get_stats(dev, &before);
        CHECK(cgra_get_info(dev, &info) == CGRA_ERR_DESYNC &&
              memcmp(&info, &expected, sizeof(info)) == 0 && !cgra_has_exec(dev),
              "discovery preserves output on failure; has_exec alone conflates errors with v2");
        CHECK(cgra_vec_add(dev, a, a, out, 3) == CGRA_ERR_DESYNC &&
              cgra_scan(dev, CGRA_OP_ADD, a, 3, out, 3) == CGRA_ERR_DESYNC &&
              cgra_matvec(dev, a, 3, 1, &weight, out, 3) == CGRA_ERR_DESYNC &&
              cgra_conv(dev, &weight, 1, a, 3, out, 3) == CGRA_ERR_DESYNC &&
              cgra_dot(dev, a, a, 3, out) == CGRA_ERR_DESYNC,
              "all nonempty kernels reject the stopped session");
        cgra_get_stats(dev, &after);
        CHECK(before.transactions == after.transactions && before.retries == 0 &&
              before.tx_bytes == after.tx_bytes && before.rx_bytes == after.rx_bytes,
              "stopped kernel session sends no retries or later kernel commands");
        cgra_close(dev);
        close(master);
    }
}

static double milliseconds(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
}

static volatile sig_atomic_t signals;
static void interrupted(int sig) { (void)sig; signals++; }

static void test_write_budget(void)
{
    int master;
    cgra_t *dev = open_peer(&master);
    int filler = open(ptsname(master), O_RDWR | O_NOCTTY | O_NONBLOCK);
    CHECK(filler >= 0, "open same PTY to fill its transmit queue");
    char bytes[4096] = {0};
    size_t queued = 0;
    for (;;) {
        ssize_t n = write(filler, bytes, sizeof(bytes));
        if (n < 0) {
            assert(errno == EAGAIN);
            /* PTY input delivery runs asynchronously in the kernel. A first
             * EAGAIN can clear while bytes move to the unread master queue. */
            struct pollfd pfd = {.fd = filler, .events = POLLOUT};
            if (poll(&pfd, 1, 50) == 0) break;
            continue;
        }
        queued += (size_t)n;
    }
    CHECK(queued > 0, "real PTY backpressure before the library write");
    struct sigaction sa = {0}, old;
    sa.sa_handler = interrupted;
    sigemptyset(&sa.sa_mask);
    CHECK(sigaction(SIGUSR1, &sa, &old) == 0, "install interrupted-wait handler");
    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        pid_t parent = getppid();
        for (int i = 0; i < 40; i++) { pause_ms(2); kill(parent, SIGUSR1); }
        _exit(0);
    }
    CHECK(child > 0, "fork repeated signal source");
    cgra_set_transaction_timeout(dev, 25);
    cgra_set_timeout(dev, 1000);
    double start = milliseconds();
    int rc = cgra_identify(dev, NULL);
    double elapsed = milliseconds() - start;
    CHECK(rc == CGRA_ERR_TIMEOUT && elapsed >= 15 && elapsed < 300,
          "whole-transaction budget bounds a blocked write despite EINTR");
    cgra_diagnostic_t d;
    cgra_get_diagnostic(dev, &d);
    CHECK(d.code == rc && d.phase == CGRA_PHASE_WRITE && d.system_errno == ETIMEDOUT &&
          d.command == 1 && d.attempt == 1 && d.tx_bytes == 0 && d.rx_bytes == 0,
          "write deadline diagnostic records phase, cause, command and zero progress");
    CHECK(cgra_run(dev, 0) == CGRA_ERR_DESYNC, "write timeout stops session without retry");
    int status;
    while (waitpid(child, &status, 0) < 0) assert(errno == EINTR);
    CHECK(signals > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "signals actually interrupted the wait");
    sigaction(SIGUSR1, &old, NULL);
    cgra_close(dev); close(filler); close(master);
    CHECK(d.command == 1 && d.phase == CGRA_PHASE_WRITE,
          "diagnostic is an owned copy after closing the handle");
}

static void test_transaction_read_budget(void)
{
    int master;
    cgra_t *dev = open_peer(&master);
    cgra_set_timeout(dev, 1000);
    cgra_set_transaction_timeout(dev, 20);
    double start = milliseconds();
    CHECK(cgra_identify(dev, NULL) == CGRA_ERR_TIMEOUT && milliseconds() - start < 300,
          "transaction budget also shortens the reply wait");
    cgra_diagnostic_t d;
    cgra_get_diagnostic(dev, &d);
    CHECK(d.phase == CGRA_PHASE_READ && d.system_errno == ETIMEDOUT && d.tx_bytes == 1,
          "read deadline diagnostic distinguishes a transmitted request");
    cgra_clear_diagnostic(dev);
    cgra_get_diagnostic(dev, &d);
    CHECK(d.code == CGRA_OK && d.phase == CGRA_PHASE_NONE,
          "diagnostic can be cleared explicitly");
    cgra_close(dev); close(master);
}

static void test_partial_write_and_retry_budget(void)
{
    int master;
    cgra_t *dev = open_peer(&master);
    const unsigned char id[] = {0xCA, 3, 1, 1, 16};
    unsigned char wire[16];
    CHECK(write(master, id, sizeof(id)) == sizeof(id) &&
          cgra_get_info(dev, NULL) == CGRA_OK && read(master, wire, 1) == 1,
          "establish geometry for partial-write regression");
    unsigned char ack = 0x79;
    CHECK(write(master, &ack, 1) == 1, "queue complete ACK");
    uint32_t cfg = CGRA_CFG(CGRA_OP_CONST, 0, 0, -123);
    fragment_writes = inject_interrupt = inject_again = 1;
    int rc = cgra_configure_n(dev, &cfg, 1);
    fragment_writes = 0;
    CHECK(rc == CGRA_OK && !inject_interrupt && !inject_again &&
          read(master, wire, sizeof(wire)) == 6 && wire[0] == 2 && wire[1] == 0x85,
          "partial writes, EINTR and EAGAIN preserve each serialized byte");
    cgra_set_transaction_timeout(dev, 30);
    cgra_set_timeout(dev, 1000);
    cgra_set_retries(dev, 5);
    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        alarm(2);
        for (int i = 0; i < 5; i++) {
            pause_ms(20);
            unsigned char nack = 0x1f;
            if (write(master, &nack, 1) != 1) _exit(1);
        }
        _exit(0);
    }
    CHECK(child > 0, "fork delayed NACK source");
    double start = milliseconds();
    rc = cgra_configure_n(dev, &cfg, 1);
    double elapsed = milliseconds() - start;
    cgra_diagnostic_t d;
    cgra_get_diagnostic(dev, &d);
    CHECK(rc == CGRA_ERR_TIMEOUT && elapsed < 90 && d.attempt == 2 &&
          d.tx_bytes == 12 && d.rx_bytes == 1 && d.phase == CGRA_PHASE_READ,
          "NACK retry shares original budget and diagnostic cumulative progress");
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status),
          "NACK source finished");
    cgra_close(dev); close(master);
}

static void test_open_flags(const char *self)
{
    int master;
    cgra_t *dev = open_peer(&master);
    struct stat tty, st;
    CHECK(stat(ptsname(master), &tty) == 0, "stat slave for descriptor inspection");
    int serial = -1;
    for (int fd = 3; fd < 256; fd++)
        if (fstat(fd, &st) == 0 && tty.st_dev == st.st_dev && tty.st_ino == st.st_ino)
            serial = fd;
    CHECK(serial >= 0 && (fcntl(serial, F_GETFD) & FD_CLOEXEC) &&
          (fcntl(serial, F_GETFL) & O_NONBLOCK), "serial fd is nonblocking and close-on-exec");
    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        char arg[24];
        snprintf(arg, sizeof(arg), "%d", serial);
        execl(self, self, "--closed", arg, (char *)NULL);
        _exit(2);
    }
    int status;
    CHECK(child > 0 && waitpid(child, &status, 0) == child &&
          WIFEXITED(status) && !WEXITSTATUS(status), "serial descriptor does not survive exec");
    cgra_close(dev);
    dev = cgra_open_ex(ptsname(master), 115200, CGRA_OPEN_EXCLUSIVE);
    CHECK(dev != NULL, "optional exclusive serial open succeeds");
    errno = 0;
    int another = open(ptsname(master), O_RDWR | O_NOCTTY);
    CHECK(another == -1 && errno == EBUSY, "exclusive ownership rejects a subsequent ordinary open");
    cgra_close(dev);
    another = open(ptsname(master), O_RDWR | O_NOCTTY);
    CHECK(another >= 0, "close releases exclusive access even while PTY master remains open");
    close(another); close(master);
    CHECK(cgra_open_ex("sim", 0, CGRA_OPEN_EXCLUSIVE) == NULL && errno == EINVAL &&
          cgra_open_ex("sim", 0, 2) == NULL && errno == EINVAL,
          "unsupported open flag combinations fail before opening");
}

static void server_ready(const char *path, void *user)
{
    int fd = *(int *)user;
    assert(write(fd, path, strlen(path) + 1) == (ssize_t)strlen(path) + 1);
}

static void test_configurable_server(void)
{
    for (int scenario = 0; scenario < 4; scenario++) {
        int ready[2], stop[2];
        CHECK(pipe(ready) == 0 && pipe(stop) == 0, "create server ready/cancellation channels");
        fflush(stdout);
        pid_t child = fork();
        if (child == 0) {
            alarm(5);
            close(ready[0]); close(stop[1]);
            cgra_pty_options_t opts = {2, 3, scenario == 0 ? 2u : 3u, stop[0]};
            int rc = cgra_emulate_pty_ex(&opts, server_ready, &ready[1]);
            /* The service borrows the cancellation fd, never closes it. */
            int owned = fcntl(stop[0], F_GETFD) >= 0;
            close(ready[1]); close(stop[0]);
            _exit(rc == CGRA_OK && owned ? 0 : 1);
        }
        CHECK(child > 0, "fork configurable PTY service");
        close(ready[1]); close(stop[0]);
        char path[128];
        ssize_t n = read(ready[0], path, sizeof(path));
        CHECK(n > 0 && path[n - 1] == 0, "receive borrowed PTY path through callback");
        cgra_t *dev = cgra_open(path, 115200);
        cgra_info_t info;
        int16_t a[] = {-32768, 3, 32767}, b[] = {-1, -7, 2}, out[3];
        CHECK(dev && cgra_get_info(dev, &info) == CGRA_OK && info.rows == 2 &&
              info.cols == 3 && info.version == (scenario == 0 ? 2 : 3) &&
              cgra_vec_add(dev, a, b, out, 3) == CGRA_OK &&
              out[0] == 32767 && out[1] == -4 && out[2] == -32767,
              "configured v2/v3 geometry serves a real serial API workload");
        int raw = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
        CHECK(raw >= 0, "open raw client for stream/backpressure cases");
        if (scenario == 1) {
            /* 100 one-byte IDs in one write exceeded the old 256-byte FIFO. */
            unsigned char ids[100], reply[500];
            memset(ids, 1, sizeof(ids));
            CHECK(write(raw, ids, sizeof(ids)) == sizeof(ids), "batch 100 consecutive ID commands");
            size_t got = 0;
            while (got < sizeof(reply)) {
                struct pollfd pfd = {.fd = raw, .events = POLLIN};
                assert(poll(&pfd, 1, 1000) == 1);
                ssize_t k = read(raw, reply + got, sizeof(reply) - got);
                assert(k > 0); got += (size_t)k;
            }
            int valid = 1;
            for (size_t i = 0; i < sizeof(reply); i += 5)
                if (memcmp(reply + i, "\312\3\2\3\20", 5)) valid = 0;
            CHECK(valid, "all 500 response bytes survive without model FIFO overflow");
        } else if (scenario == 2) {
            const unsigned char partial[] = {2, 0x12};
            CHECK(write(raw, partial, sizeof(partial)) == sizeof(partial),
                  "leave a CFG frame incomplete before cancellation");
        } else if (scenario == 3) {
            unsigned char requests[8192];
            memset(requests, 5, sizeof(requests));
            CHECK(write(raw, requests, sizeof(requests)) > 0,
                  "flood readback requests without draining responses");
            pause_ms(30); /* server reaches TX backpressure */
        }
        CHECK(write(stop[1], "s", 1) == 1, "request controlled stop");
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status),
              "server stops cleanly from idle, incomplete RX or blocked TX; stop fd retained");
        cgra_close(dev); close(raw); close(ready[0]); close(stop[1]);
    }
    cgra_pty_options_t bad = {0, 4, 3, -1};
    CHECK(cgra_emulate_pty_ex(&bad, NULL, NULL) == CGRA_ERR_ARG &&
          cgra_emulate_pty_ex(NULL, NULL, NULL) == CGRA_ERR_ARG,
          "invalid service options rejected before callback/device creation");
}

static void test_diagnostic_persistence(void)
{
    cgra_t *dev = cgra_open("sim:1x1:flaky", 0);
    uint32_t cfg = 0;
    cgra_set_retries(dev, 0);
    CHECK(cgra_configure_n(dev, &cfg, 1) == CGRA_ERR_NACK,
          "complete NACK available for diagnostic persistence check");
    cgra_diagnostic_t before, after;
    cgra_get_diagnostic(dev, &before);
    CHECK(before.code == CGRA_ERR_NACK && before.phase == CGRA_PHASE_VALIDATE &&
          before.command == 2 && before.system_errno == 0 && before.attempt == 1 &&
          before.tx_bytes == 6 && before.rx_bytes == 1, "NACK diagnostic is distinct from syscall failure");
    CHECK(cgra_configure_n(dev, &cfg, 1) == CGRA_OK && cgra_run(NULL, 1) == CGRA_ERR_ARG,
          "subsequent success and invalid call do not overwrite last failure");
    cgra_get_diagnostic(dev, &after);
    CHECK(before.code == after.code && before.phase == after.phase && before.command == after.command &&
          before.tx_bytes == after.tx_bytes, "stored diagnostic survives later success");
    cgra_close(dev);
}

static void test_plan_uncertain(void)
{
    int master; cgra_t *dev = open_peer(&master);
    cgra_set_timeout(dev,10);
    const unsigned char ready[] = {0xCA,3,1,1,16,0x79};
    uint32_t cfg=0; cgra_plan_spec_t spec={1,1,1,CGRA_EXEC_RESET,1};
    cgra_plan_t *plan=NULL;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan)==CGRA_OK,"compile serial plan");
    CHECK(write(master,ready,sizeof(ready))==sizeof(ready) && cgra_plan_bind(dev,plan)==CGRA_OK,
          "serial plan binding consumes ID and CFG acknowledgement");
    unsigned char wire[64];
    CHECK(read(master,wire,sizeof(wire))==7,"drain exactly ID and CFG requests");
    int16_t out=123;
    CHECK(cgra_plan_execute_n(dev,plan,NULL,0,NULL,0,&out,1)==CGRA_ERR_TIMEOUT && out==123,
          "uncertain planned execution returns original timeout without output");
    CHECK(read(master,wire,sizeof(wire))==10,"uncertain execution sent once, even with RESET");
    cgra_stats_t before,after; cgra_get_stats(dev,&before);
    CHECK(cgra_plan_execute_n(dev,plan,NULL,0,NULL,0,&out,1)==CGRA_ERR_DESYNC &&
          cgra_plan_bind(dev,plan)==CGRA_ERR_DESYNC,"poisoned plan cannot execute or rebind");
    cgra_get_stats(dev,&after);
    struct pollfd pfd={.fd=master,.events=POLLIN};
    CHECK(before.tx_bytes==after.tx_bytes && poll(&pfd,1,0)==0,"blocked plan performs no automatic recovery");
    cgra_diagnostic_t diagnostic; cgra_get_diagnostic(dev,&diagnostic);
    CHECK(diagnostic.code==CGRA_ERR_TIMEOUT && diagnostic.command==7 && diagnostic.attempt==1,
          "planned execution preserves owned wire failure diagnostic");
    cgra_plan_free(plan); cgra_close(dev); close(master);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--closed") == 0)
        return fcntl(atoi(argv[2]), F_GETFD) == -1 && errno == EBADF ? 0 : 1;
    alarm(10);
    test_plan_uncertain();
    test_silent_reply();
    test_fragmented_reply();
    test_failed_write();
    test_uncertain_session();
    test_high_descriptor();
    test_kernel_partial_output();
    test_write_budget();
    test_transaction_read_budget();
    test_partial_write_and_retry_budget();
    test_open_flags(argv[0]);
    test_configurable_server();
    test_diagnostic_persistence();
    printf("# %d transport checks passed\n", checks);
    return 0;
}
