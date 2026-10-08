#define _GNU_SOURCE
#include <errno.h>
#include <glob.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cli.h"
#include "compile.h"
#include "paths.h"

static int looks_literal(const char *s)
{
    return strchr(s, '/') != NULL || strncmp(s, "sim", 3) == 0;
}

/* Path of the file where `cgra probe` records the discovered device. */
static int device_state_path(char *buf, size_t sz)
{
    char udir[DSL_VAL];
    if (dsl_user_dir(udir, sizeof(udir)) != 0)
        return -1;
    snprintf(buf, sz, "%s/.device", udir);
    return 0;
}

static int read_device_state(char *buf, size_t sz)
{
    char p[DSL_VAL + 16];
    if (device_state_path(p, sizeof(p)) != 0)
        return -1;
    FILE *f = fopen(p, "r");
    if (!f)
        return -1;
    int ok = fgets(buf, (int)sz, f) != NULL;
    fclose(f);
    if (!ok)
        return -1;
    buf[strcspn(buf, "\r\n")] = '\0';
    return buf[0] ? 0 : -1;
}

cgra_t *cli_open_device_resolved(dsl_ctx *d, const char *name_opt, char *err,
                                     size_t errsz, connection_info *details)
{
    const char *name = name_opt;
    if (!name) name = getenv("CGRA_DEVICE");
    if (!name || !*name) name = "default";

    /* "auto" resolves to the port saved by `cgra probe`. */
    char autobuf[DSL_VAL];
    if (strcmp(name, "auto") == 0) {
        if (read_device_state(autobuf, sizeof(autobuf)) != 0) {
            snprintf(err, errsz, "no saved device; run `cgra probe` first");
            return NULL;
        }
        name = autobuf;
    }

    char port[DSL_VAL] = {0};
    int baud = 115200, timeout = 2000;

    if (looks_literal(name)) {
        if (strlen(name) >= sizeof(port)) {
            snprintf(err, errsz, "device path is too long"); return NULL;
        }
        snprintf(port, sizeof(port), "%s", name);
    } else {
        dsl_device *dev = dsl_find_device(d, name);
        if (!dev) { snprintf(err, errsz, "unknown device profile '%.80s'", name); return NULL; }
        snprintf(port, sizeof(port), "%s", dev->port[0] ? dev->port : "/dev/ttyUSB1");
        if (dev->baud) baud = dev->baud;
        timeout = dev->timeout_ms;
    }

    const char *env_port = getenv("CGRA_PORT");
    if (env_port && *env_port) {
        if (strlen(env_port) >= sizeof(port)) {
            snprintf(err, errsz, "CGRA_PORT is too long"); return NULL;
        }
        snprintf(port, sizeof(port), "%s", env_port);
    }
    const char *env_baud = getenv("CGRA_BAUD");
    if (env_baud && *env_baud) {
        long value;
        if (dsl_integer(env_baud, 1, INT_MAX, &value)) {
            snprintf(err, errsz, "CGRA_BAUD requires a positive integer");
            return NULL;
        }
        baud = (int)value;
    }

    cgra_t *dev = cgra_open(port, (unsigned)baud);
    if (!dev) { snprintf(err, errsz, "cannot open %s at %d baud", port, baud); return NULL; }
    cgra_set_timeout(dev, (unsigned)timeout);
    if (details) {
        snprintf(details->port, sizeof(details->port), "%s", port);
        details->baud = baud; details->timeout_ms = timeout;
    }
    return dev;
}

cgra_t *cli_open_device(dsl_ctx *d, const char *name, char *err, size_t errsz)
{
    return cli_open_device_resolved(d, name, err, errsz, NULL);
}

int cli_mkdir_p(const char *path)
{
    char tmp[DSL_VAL];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void emulate_ready(const char *path, void *user)
{
    (void)user;
    printf("cgra emulator serving on %s\n", path);
    printf("point tools at it, e.g.:  cgra -d %s ping   (Ctrl-C to stop)\n", path);
    fflush(stdout);
}

/* Signals wake the poll loop through a self-pipe; handler only uses write. */
static volatile sig_atomic_t stop_write_fd = -1;
static void stop_emulator(int signal_number)
{
    (void)signal_number;
    int saved = errno;
    if (stop_write_fd >= 0) {
        ssize_t ignored = write(stop_write_fd, "s", 1);
        (void)ignored;
    }
    errno = saved;
}

int cli_cmd_emulate(unsigned rows, unsigned cols, unsigned version, unsigned flags)
{
    if (!rows || !cols || rows > CGRA_MAX_EDGE || cols > CGRA_MAX_EDGE ||
        rows * cols > CGRA_MAX_PE || (version != 2 && version != 3)) {
        fprintf(stderr, "cgra: emulate requires 1..16 PEs and protocol 2 or 3\n");
        return 1;
    }
    int stop[2];
    if (pipe2(stop, O_CLOEXEC | O_NONBLOCK) != 0) { perror("cgra: stop pipe"); return 1; }
    struct sigaction sa = {0}, old_int, old_term;
    sa.sa_handler = stop_emulator;
    sigemptyset(&sa.sa_mask);
    stop_write_fd = stop[1];
    if (sigaction(SIGINT, &sa, &old_int) != 0) {
        stop_write_fd = -1; close(stop[0]); close(stop[1]); return 1;
    }
    if (sigaction(SIGTERM, &sa, &old_term) != 0) {
        sigaction(SIGINT, &old_int, NULL);
        stop_write_fd = -1; close(stop[0]); close(stop[1]); return 1;
    }
    cgra_pty_options_t options = {rows, cols, version, stop[0]};
    int rc = cgra_emulate_pty_with_flags(&options, flags, emulate_ready, NULL);
    sigaction(SIGINT, &old_int, NULL); sigaction(SIGTERM, &old_term, NULL);
    stop_write_fd = -1;
    close(stop[0]); close(stop[1]);
    if (rc != CGRA_OK) {
        fprintf(stderr, "cgra: emulator error: %s\n", cgra_strerror(rc));
        return 1;
    }
    return 0;
}

/* -------------------------------------------------- known-answer checks */

/* Runs every kernel/pattern on an already-open device and checks it against a
 * host reference. Prints one line per check; returns the number of failures.
 * Shared by `selftest` (emulator) and `probe` (real device bring-up). */
static int device_checks(cgra_t *dev, dsl_ctx *d)
{
    char err[DSL_VAL];
    int16_t a[16], b[16], out[16];
    for (int i = 0; i < 16; i++) { a[i] = (int16_t)(i - 5); b[i] = (int16_t)(2 * i + 1); }

    int fails = 0;
    struct { const char *mode; long imm; int has_imm; } cases[] = {
        {"add", 0, 0}, {"sub", 0, 0}, {"mul", 0, 0},
        {"min", 0, 0}, {"max", 0, 0}, {"relu", 0, 0},
        {"addi", 100, 1}, {"muli", 3, 1},
    };
    for (size_t t = 0; t < sizeof(cases) / sizeof(cases[0]); t++) {
        dsl_mode *m = dsl_find_mode(d, cases[t].mode);
        int n = mode_run(dev, m, a, b, 16, cases[t].imm, cases[t].has_imm, out, 16, err, sizeof(err));
        if (n < 0) { printf("FAIL  %-6s %s\n", cases[t].mode, err); fails++; continue; }
        int ok = 1;
        for (int i = 0; i < 16; i++) {
            int16_t e;
            const char *mo = cases[t].mode;
            if (!strcmp(mo, "add")) e = (int16_t)(a[i] + b[i]);
            else if (!strcmp(mo, "sub")) e = (int16_t)(a[i] - b[i]);
            else if (!strcmp(mo, "mul")) e = (int16_t)(a[i] * b[i]);
            else if (!strcmp(mo, "min")) e = a[i] < b[i] ? a[i] : b[i];
            else if (!strcmp(mo, "max")) e = a[i] > b[i] ? a[i] : b[i];
            else if (!strcmp(mo, "relu")) e = a[i] > 0 ? a[i] : 0;
            else if (!strcmp(mo, "addi")) e = (int16_t)(a[i] + 100);
            else e = (int16_t)(a[i] * 3);
            if (out[i] != e) { ok = 0; break; }
        }
        printf("%-5s %-6s\n", ok ? "ok" : "FAIL", cases[t].mode);
        if (!ok) fails++;
    }

    /* reduction */
    dsl_mode *dotm = dsl_find_mode(d, "dot");
    int n = mode_run(dev, dotm, a, b, 16, 0, 0, out, 16, err, sizeof(err));
    int16_t acc = 0;
    for (int i = 0; i < 16; i++) acc = (int16_t)(acc + (int16_t)(a[i] * b[i]));
    if (n == 1 && out[0] == acc) printf("ok    dot = %d\n", out[0]);
    else { printf("FAIL  dot (%d vs %d)\n", n == 1 ? out[0] : -1, acc); fails++; }

    /* systolic matrix-vector, non-square (6x5) to exercise tiling */
    {
        enum { MM = 6, NN = 5 };
        int16_t A[MM * NN], xv[NN], yv[MM], yref[MM];
        for (int i = 0; i < MM; i++) {
            for (int j = 0; j < NN; j++) A[i * NN + j] = (int16_t)(i - j + 1);
        }
        for (int j = 0; j < NN; j++) xv[j] = (int16_t)(j + 1);
        for (int i = 0; i < MM; i++) {
            int32_t s = 0;
            for (int j = 0; j < NN; j++) s += A[i * NN + j] * xv[j];
            yref[i] = (int16_t)s;
        }
        int mrc = matvec_run(dev, A, MM, NN, xv, yv, err, sizeof(err));
        int ok = (mrc == CGRA_OK) && memcmp(yv, yref, sizeof yref) == 0;
        printf("%-5s matvec 6x5\n", ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    /* prefix scan across two blocks (9 elements) */
    {
        int16_t sa[9], so[9], sref[9];
        int32_t run = 0;
        for (int i = 0; i < 9; i++) { sa[i] = (int16_t)(i - 3); run += sa[i]; sref[i] = (int16_t)run; }
        int srn = scan_run(dev, sa, 9, CGRA_OP_ADD, so, err, sizeof(err));
        int ok = (srn == 9) && memcmp(so, sref, sizeof sref) == 0;
        printf("%-5s scan(9)\n", ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    /* full 1-D convolution */
    {
        int16_t hh[3] = {1, 2, 3}, xx[5] = {4, 5, 6, 7, 8}, co[7], cref[7];
        for (int i = 0; i < 7; i++) {
            int32_t s = 0;
            for (int t = 0; t < 3; t++) { int j = i - t; if (j >= 0 && j < 5) s += hh[t] * xx[j]; }
            cref[i] = (int16_t)s;
        }
        int crn = conv_run(dev, hh, 3, xx, 5, co, 7, err, sizeof(err));
        int ok = (crn == 7) && memcmp(co, cref, sizeof cref) == 0;
        printf("%-5s conv 3*5\n", ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    return fails;
}

int cli_selftest(void)
{
    dsl_ctx d;
    dsl_init(&d);
    char err[DSL_VAL];
    if (dsl_load_defaults(&d, err, sizeof(err)) != 0) {
        fprintf(stderr, "cgra: %s\n", err); return 1;
    }

    /* Run the whole known-answer suite twice: once against a protocol v3
     * emulator (fused EXEC transactions) and once against one that reports v2
     * and rejects EXEC, so the pre-v3 fallback the library keeps for older
     * bitstreams is held to exactly the same results. */
    static const struct { const char *dev, *label; } backends[] = {
        { "sim:",   "v3 (fused exec)" },
        { "sim:v2", "v2 (write/run/read)" },
    };
    int fails = 0;
    for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
        cgra_t *dev = cgra_open(backends[i].dev, 115200);
        if (!dev) { fprintf(stderr, "cgra: cannot open emulator\n"); return 1; }
        printf("-- protocol %s --\n", backends[i].label);
        fails += device_checks(dev, &d);
        cgra_close(dev);
    }

    /* auto-retry path: sim:flaky NACKs the first CFG and WR; the result must
     * still be correct and the stats must show the retries. */
    cgra_t *fdev = cgra_open("sim:flaky", 115200);
    if (fdev) {
        int16_t fa[4] = {1, 2, 3, 4}, fb[4] = {10, 20, 30, 40}, fo[4];
        int rc = cgra_vec_add(fdev, fa, fb, fo, 4);
        cgra_stats_t s; cgra_get_stats(fdev, &s);
        int ok = (rc == CGRA_OK) && fo[0] == 11 && fo[3] == 44 && s.retries == 2;
        printf("%-5s retry-on-nack (retries=%lu)\n", ok ? "ok" : "FAIL", s.retries);
        if (!ok) fails++;
        cgra_close(fdev);
    }

    printf(fails ? "%d test(s) FAILED\n" : "all tests PASSED\n", fails);
    return fails ? 1 : 0;
}

static void save_device_state(const char *port)
{
    char udir[DSL_VAL], p[DSL_VAL + 16];
    if (dsl_user_dir(udir, sizeof(udir)) != 0)
        return;
    cli_mkdir_p(udir);
    if (device_state_path(p, sizeof(p)) != 0)
        return;
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s\n", port); fclose(f); }
}

/* Scan the common USB-serial device paths, ping each, and report any CGRA
 * found; the first is recorded so later commands can use `-d auto`. */
static int probe_scan(void)
{
    static const char *pats[] = { "/dev/ttyUSB*", "/dev/ttyACM*" };
    char first[DSL_VAL] = {0};
    int found = 0;

    for (size_t p = 0; p < sizeof(pats) / sizeof(pats[0]); p++) {
        glob_t g;
        if (glob(pats[p], 0, NULL, &g) != 0)
            continue;
        for (size_t i = 0; i < g.gl_pathc; i++) {
            cgra_t *dev = cgra_open(g.gl_pathv[i], 115200);
            if (!dev)
                continue;
            cgra_set_timeout(dev, 300);
            cgra_info_t info;
            if (cgra_identify(dev, &info) == CGRA_OK) {
                printf("found CGRA v%u on %s (%ux%u, %u-bit)\n",
                       info.version, g.gl_pathv[i], info.rows, info.cols, info.data_w);
                if (!found) { snprintf(first, sizeof(first), "%s", g.gl_pathv[i]); found = 1; }
            }
            cgra_close(dev);
        }
        globfree(&g);
    }

    if (found) {
        save_device_state(first);
        printf("saved %s — use it with `cgra -d auto ...`\n", first);
        return 0;
    }
    fprintf(stderr, "cgra: no CGRA found on /dev/ttyUSB* or /dev/ttyACM*\n");
    return 1;
}

/* Bring-up check: with no device, scan the serial ports; with a device,
 * identify it and run the known-answer tests. */
int cli_cmd_probe(dsl_ctx *d, const char *devname)
{
    if (devname == NULL && getenv("CGRA_DEVICE") == NULL)
        return probe_scan();

    char err[DSL_VAL];
    cgra_t *dev = cli_open_device(d, devname, err, sizeof(err));
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }

    cgra_info_t info;
    if (cgra_identify(dev, &info) != CGRA_OK) {
        fprintf(stderr, "cgra: no response from device (check wiring / baud)\n");
        cgra_close(dev);
        return 1;
    }
    printf("device: protocol v%u, %ux%u array, %u-bit datapath\n",
           info.version, info.rows, info.cols, info.data_w);
    int supported = cgra_get_info(dev, NULL);
    if (supported != CGRA_OK) {
        fprintf(stderr, "cgra: %s\n", cgra_strerror(supported));
        cgra_close(dev);
        return 1;
    }

    int fails = device_checks(dev, d);
    cgra_close(dev);
    printf(fails ? "%d check(s) FAILED\n" : "device OK\n", fails);
    return fails ? 1 : 0;
}
