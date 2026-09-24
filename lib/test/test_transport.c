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
#include <sys/select.h>   /* FD_SETSIZE: regression threshold, not transport I/O */
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int checks;
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

int main(void)
{
    alarm(10);
    test_silent_reply();
    test_fragmented_reply();
    test_failed_write();
    test_uncertain_session();
    test_high_descriptor();
    test_kernel_partial_output();
    printf("# %d transport checks passed\n", checks);
    return 0;
}
