/* Scaling experiments for the report. Counts commands/bytes through the PUBLIC
 * API on the clockless emulator; no elapsed time, FPGA timing or speedup.
 * Every point is checked against an independent scalar reference first.
 *
 *   repeat:   4x4 dot product, n in {16,64,256,1024}, r repeated calls,
 *             protocol v2, v3 and resident blocks (preflight/HELLO/upload once)
 *   geometry: five 16-PE shapes x five kernels x protocol v2/v3, one call
 */
#include "cgra.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "scaling experiment: %s\n", what); exit(1); }
}

static int16_t dot_ref(const int16_t *a, const int16_t *b, size_t n)
{
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += (int64_t)a[i] * b[i];
    return (int16_t)(uint16_t)((uint64_t)s & 0xFFFFu);
}

static void row(const char *exp, const char *geo, const char *kernel, const char *proto,
                size_t n, unsigned repeat, const cgra_stats_t *s)
{
    printf("%s,%s,%s,%s,%zu,%u,%llu,%llu,%llu\n", exp, geo, kernel, proto, n, repeat,
           (unsigned long long)s->transactions, (unsigned long long)s->tx_bytes,
           (unsigned long long)s->rx_bytes);
}

static cgra_t *open_checked(const char *device, unsigned rows, unsigned cols, int version)
{
    cgra_t *dev = cgra_open(device, 115200);
    require(dev != NULL, device);
    cgra_info_t info;
    require(cgra_get_info(dev, &info) == CGRA_OK && info.rows == rows && info.cols == cols &&
            (version == 0 || info.version == version), "identify emulator");
    return dev;
}

int main(void)
{
    enum { NMAX = 1024 };
    static int16_t a[NMAX], b[NMAX];
    for (int i = 0; i < NMAX; i++) { a[i] = (int16_t)(i % 7 - 3); b[i] = (int16_t)(i % 5 - 2); }
    puts("experiment,geometry,kernel,protocol,n,repeat,transactions,tx_bytes,rx_bytes");

    const size_t lengths[] = {16, 64, 256, 1024};
    const unsigned repeats[] = {1, 2, 4, 8, 16, 32, 64};
    for (size_t li = 0; li < 4; li++) for (size_t ri = 0; ri < 7; ri++) {
        size_t n = lengths[li];
        unsigned r = repeats[ri];
        int16_t expected = dot_ref(a, b, n), result;
        cgra_stats_t s;
        for (int version = 2; version <= 3; version++) {
            cgra_t *dev = open_checked(version == 2 ? "sim:v2:4x4" : "sim:4x4", 4, 4, version);
            cgra_reset_stats(dev);   /* exclude the one initial ID, as in Table VIII */
            for (unsigned i = 0; i < r; i++) {
                result = 0x5a5a;
                require(cgra_dot(dev, a, b, n, &result) == CGRA_OK && result == expected, "legacy dot");
            }
            cgra_get_stats(dev, &s);
            require(s.retries == 0, "unexpected retry");
            row("repeat", "4x4", "dot", version == 2 ? "v2" : "v3", n, r, &s);
            cgra_close(dev);
        }
        cgra_t *dev = cgra_open("sim:blocks:4x4", 115200);
        require(dev != NULL, "open block emulator");
        cgra_reset_stats(dev);       /* blocks: count preflight, HELLO and uploads */
        require(cgra_block_begin(dev, 0x01020304u) == CGRA_OK, "block begin");
        require(cgra_block_upload(dev, 0, a, n) == CGRA_OK &&
                cgra_block_upload(dev, 1, b, n) == CGRA_OK, "block upload");
        for (unsigned i = 0; i < r; i++) {
            uint32_t executions = 0;
            result = 0x5a5a;
            require(cgra_block_dot(dev, n, &result, &executions) == CGRA_OK &&
                    result == expected && executions == i + 1, "block dot");
        }
        cgra_get_stats(dev, &s);
        require(s.retries == 0, "unexpected retry");
        row("repeat", "4x4", "dot", "blocks", n, r, &s);
        cgra_close(dev);
    }

    const char *shapes[] = {"1x16", "2x8", "4x4", "8x2", "16x1"};
    const unsigned srows[] = {1, 2, 4, 8, 16}, scols[] = {16, 8, 4, 2, 1};
    static int16_t matrix[32 * 32];
    for (int i = 0; i < 32 * 32; i++) matrix[i] = (int16_t)(i % 7 + 1);
    const int16_t h[5] = {1, -2, 3, -4, 5};
    const char *kernels[] = {"vector", "dot", "scan", "matvec", "conv"};
    for (int g = 0; g < 5; g++) for (int kind = 0; kind < 5; kind++)
    for (int version = 2; version <= 3; version++) {
        char device[32];
        snprintf(device, sizeof device, "sim:%s%s", version == 2 ? "v2:" : "", shapes[g]);
        cgra_t *dev = open_checked(device, srows[g], scols[g], version);
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
            ref[r] = (int16_t)(ref[r] + (int32_t)matrix[r * n + c] * b[c]);
        if (kind == 4) for (size_t i = 0; i < outputs; i++) for (size_t j = 0; j < k; j++)
            if (i >= j && i - j < n) ref[i] = (int16_t)(ref[i] + (int32_t)h[j] * b[i - j]);
        out[outputs] = 12345;
        cgra_reset_stats(dev);
        int rc;
        switch (kind) {
        case 0: rc = cgra_vec_add(dev, a, b, out, n); break;
        case 1: rc = cgra_dot(dev, a, b, n, out); break;
        case 2: rc = cgra_scan(dev, CGRA_OP_ADD, a, n, out, outputs); break;
        case 3: rc = cgra_matvec(dev, matrix, m, n, b, out, outputs); break;
        default: rc = cgra_conv(dev, h, k, b, n, out, outputs); break;
        }
        require(rc == CGRA_OK, cgra_strerror(rc));
        require(memcmp(out, ref, outputs * sizeof *out) == 0 && out[outputs] == 12345,
                "scalar comparison or output canary failed");
        cgra_stats_t s;
        cgra_get_stats(dev, &s);
        require(s.retries == 0, "unexpected retry");
        row("geometry", shapes[g], kernels[kind], version == 2 ? "v2" : "v3", n, 1, &s);
        cgra_close(dev);
    }
    return ferror(stdout) ? 1 : 0;
}
