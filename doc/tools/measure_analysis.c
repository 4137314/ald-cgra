/* Analysis driver for the manuscript (counts only; clockless emulator).
 * Uses only the public API, including the optional profiling counters for
 * requested array steps. Every result is checked against a scalar reference.
 *
 * Output (CSV on stdout), two record kinds:
 *  kernel,<shape>,<kernel>,<v2|v3>,n,transactions,tx,rx,steps,cfg_transactions
 *  reuse,4x4,<scenario>,blocks|v3,n,r,transactions,tx,rx
 * Reuse scenarios for r dot products of length n:
 *  none    both banks uploaded before every DOT (framing/batching, no reuse)
 *  one     bank A uploaded once, bank B re-uploaded before every DOT
 *  both    both banks uploaded once (repeated identical DOT; amortisation)
 *  stream  fused legacy kernel (v3), one call per DOT
 */
#include "cgra.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "analysis: %s\n", what); exit(1); }
}

static int16_t dot_ref(const int16_t *a, const int16_t *b, size_t n)
{
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += (int64_t)a[i] * b[i];
    return (int16_t)(uint16_t)((uint64_t)s & 0xFFFFu);
}

enum { NMAX = 1024, RMAX = 64 };
static int16_t a[NMAX], b[NMAX], bs[RMAX][NMAX];

static void reuse_point(const char *scenario, size_t n, unsigned r)
{
    cgra_stats_t s;
    if (!strcmp(scenario, "stream")) {
        cgra_t *dev = cgra_open("sim:4x4", 115200);
        cgra_info_t info;
        require(dev && cgra_get_info(dev, &info) == CGRA_OK && info.version == 3, "open v3");
        cgra_reset_stats(dev);
        for (unsigned i = 0; i < r; i++) {
            int16_t res = 0;
            require(cgra_dot(dev, a, bs[i], n, &res) == CGRA_OK && res == dot_ref(a, bs[i], n), "v3 dot");
        }
        cgra_get_stats(dev, &s);
        cgra_close(dev);
    } else {
        cgra_t *dev = cgra_open("sim:blocks:4x4", 115200);
        require(dev != NULL, "open blocks");
        cgra_reset_stats(dev);
        require(cgra_block_begin(dev, 0x01020304u) == CGRA_OK, "begin");
        int reuse_a = strcmp(scenario, "none") != 0, reuse_b = !strcmp(scenario, "both");
        if (reuse_a) require(cgra_block_upload(dev, 0, a, n) == CGRA_OK, "upload a");
        if (reuse_b) require(cgra_block_upload(dev, 1, bs[0], n) == CGRA_OK, "upload b");
        for (unsigned i = 0; i < r; i++) {
            const int16_t *bi = reuse_b ? bs[0] : bs[i];
            if (!reuse_a) require(cgra_block_upload(dev, 0, a, n) == CGRA_OK, "upload a");
            if (!reuse_b) require(cgra_block_upload(dev, 1, bi, n) == CGRA_OK, "upload b");
            int16_t res = 0; uint32_t ex = 0;
            require(cgra_block_dot(dev, n, &res, &ex) == CGRA_OK && res == dot_ref(a, bi, n) &&
                    ex == i + 1, "block dot");
        }
        cgra_get_stats(dev, &s);
        cgra_close(dev);
    }
    require(s.retries == 0, "retry");
    printf("reuse,4x4,%s,%s,%zu,%u,%lu,%lu,%lu\n", scenario,
           strcmp(scenario, "stream") ? "blocks" : "v3", n, r, s.transactions, s.tx_bytes, s.rx_bytes);
}

int main(void)
{
    for (int i = 0; i < NMAX; i++) { a[i] = (int16_t)(i % 7 - 3); b[i] = (int16_t)(i % 5 - 2); }
    for (int k = 0; k < RMAX; k++)
        for (int i = 0; i < NMAX; i++) bs[k][i] = (int16_t)((i * 3 + k * 11) % 9 - 4);
    puts("record,geometry,kernel,protocol,n,repeat,transactions,tx_bytes,rx_bytes,steps,cfg_transactions");

    const char *shapes[] = {"1x16", "2x8", "4x4", "8x2", "16x1"};
    const unsigned rows[] = {1, 2, 4, 8, 16}, cols[] = {16, 8, 4, 2, 1};
    static int16_t matrix[32 * 32];
    for (int i = 0; i < 32 * 32; i++) matrix[i] = (int16_t)(i % 7 + 1);
    const int16_t h[5] = {1, -2, 3, -4, 5};
    /* x for matvec/conv is b' = i%7+1 (no zeros): every column tile executes,
     * as in Table VIII, so the dense closed forms apply on every shape. */
    int16_t x[256];
    for (int i = 0; i < 256; i++) x[i] = (int16_t)(i % 7 + 1);
    const char *kernels[] = {"vector", "dot", "scan", "matvec", "conv"};
    for (int g = 0; g < 5; g++) for (int kind = 0; kind < 5; kind++)
    for (int version = 2; version <= 3; version++) {
        char device[32];
        snprintf(device, sizeof device, "sim:%s%s", version == 2 ? "v2:" : "", shapes[g]);
        cgra_t *dev = cgra_open(device, 115200);
        cgra_info_t info;
        require(dev && cgra_get_info(dev, &info) == CGRA_OK && info.rows == rows[g] &&
                info.cols == cols[g] && info.version == version, "identify");
        int16_t out[257], ref[257];
        size_t n = kind == 3 ? 32 : kind == 4 ? 64 : 256;
        size_t m = kind == 3 ? 32 : 0, k = kind == 4 ? 5 : 0;
        size_t outputs = kind == 1 ? 1 : kind == 3 ? m : kind == 4 ? n + k - 1 : n;
        memset(out, 0x5a, sizeof out);
        memset(ref, 0, sizeof ref);
        if (kind == 0) for (size_t i = 0; i < n; i++) ref[i] = (int16_t)(a[i] + b[i]);
        if (kind == 1) ref[0] = dot_ref(a, b, n);
        if (kind == 2) for (size_t i = 0; i < n; i++) ref[i] = (int16_t)(a[i] + (i ? ref[i - 1] : 0));
        if (kind == 3) for (size_t r = 0; r < m; r++) for (size_t c = 0; c < n; c++)
            ref[r] = (int16_t)(ref[r] + (int32_t)matrix[r * n + c] * x[c]);
        if (kind == 4) for (size_t i = 0; i < outputs; i++) for (size_t j = 0; j < k; j++)
            if (i >= j && i - j < n) ref[i] = (int16_t)(ref[i] + (int32_t)h[j] * x[i - j]);
        out[outputs] = 12345;
        cgra_reset_stats(dev);
        cgra_set_profiling(dev, 1);
        int rc;
        switch (kind) {
        case 0: rc = cgra_vec_add(dev, a, b, out, n); break;
        case 1: rc = cgra_dot(dev, a, b, n, out); break;
        case 2: rc = cgra_scan(dev, CGRA_OP_ADD, a, n, out, outputs); break;
        case 3: rc = cgra_matvec(dev, matrix, m, n, x, out, outputs); break;
        default: rc = cgra_conv(dev, h, k, x, n, out, outputs); break;
        }
        require(rc == CGRA_OK, cgra_strerror(rc));
        require(memcmp(out, ref, outputs * sizeof *out) == 0 && out[outputs] == 12345, "scalar check");
        cgra_stats_t s; cgra_profile_t p;
        cgra_get_stats(dev, &s);
        cgra_get_profile(dev, &p);
        require(s.retries == 0 && p.valid, "retry or profile");
        printf("kernel,%s,%s,v%d,%zu,1,%lu,%lu,%lu,%llu,%llu\n", shapes[g], kernels[kind], version, n,
               s.transactions, s.tx_bytes, s.rx_bytes,
               (unsigned long long)p.requested_steps, (unsigned long long)p.configuration_transactions);
        cgra_close(dev);
    }

    const size_t lengths[] = {64, 256, 1024};
    const unsigned repeats[] = {1, 2, 4, 8, 16, 32, 64};
    const char *scen[] = {"stream", "none", "one", "both"};
    for (int li = 0; li < 3; li++) for (int ri = 0; ri < 7; ri++) for (int si = 0; si < 4; si++)
        reuse_point(scen[si], lengths[li], repeats[ri]);
    return ferror(stdout) ? 1 : 0;
}
