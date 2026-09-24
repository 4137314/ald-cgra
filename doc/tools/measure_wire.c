/* Fixed report workloads. Measure commands/bytes, not elapsed time.
 * Build against the current public library; validate independent scalar results. */
#include "cgra.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "wire experiment: %s\n", what); exit(1); }
}

int main(void)
{
    puts("workload,version,n,m,k,rows,cols,calls,transactions,tx_bytes,rx_bytes");
    const char *names[] = {"vector", "dot", "scan", "matvec", "conv"};
    for (int kind = 0; kind < 5; kind++) for (int version = 2; version <= 3; version++) {
        cgra_t *dev = cgra_open(version == 2 ? "sim:v2" : "sim:", 115200);
        require(dev != NULL, "open emulator");
        cgra_info_t info;
        require(cgra_get_info(dev, &info) == CGRA_OK && info.version == version &&
                info.rows == 4 && info.cols == 4, "identify 4x4 device");
        int16_t a[256], b[256], out[257], ref[257], matrix[32 * 32];
        for (int i = 0; i < 256; i++) { a[i] = (int16_t)(i % 19 - 9); b[i] = (int16_t)(i % 7 + 1); }
        for (int i = 0; i < 32 * 32; i++) matrix[i] = (int16_t)(i % 7 + 1);
        const int16_t h[5] = {1, -2, 3, -4, 5};
        size_t n = kind == 3 ? 32 : kind == 4 ? 64 : 256;
        size_t m = kind == 3 ? 32 : 0, k = kind == 4 ? 5 : 0;
        size_t outputs = kind == 1 ? 1 : kind == 3 ? m : kind == 4 ? n + k - 1 : n;
        memset(out, 0x5a, sizeof(out));
        memset(ref, 0, sizeof(ref));
        if (kind == 0) for (size_t i = 0; i < n; i++) ref[i] = (int16_t)(a[i] + b[i]);
        if (kind == 1) for (size_t i = 0; i < n; i++) ref[0] = (int16_t)(ref[0] + (int32_t)a[i] * b[i]);
        if (kind == 2) for (size_t i = 0; i < n; i++) ref[i] = (int16_t)(a[i] + (i ? ref[i - 1] : 0));
        if (kind == 3) for (size_t r = 0; r < m; r++) for (size_t c = 0; c < n; c++)
            ref[r] = (int16_t)(ref[r] + (int32_t)matrix[r * n + c] * b[c]);
        if (kind == 4) for (size_t i = 0; i < outputs; i++) for (size_t j = 0; j < k; j++)
            if (i >= j && i - j < n) ref[i] = (int16_t)(ref[i] + (int32_t)h[j] * b[i - j]);
        out[outputs] = 12345;
        cgra_reset_stats(dev); /* Exclude exactly one initial ID, include CFG and all compute. */
        int rc;
        switch (kind) {
        case 0: rc = cgra_vec_add(dev, a, b, out, n); break;
        case 1: rc = cgra_dot(dev, a, b, n, out); break;
        case 2: rc = cgra_scan(dev, CGRA_OP_ADD, a, n, out, outputs); break;
        case 3: rc = cgra_matvec(dev, matrix, m, n, b, out, outputs); break;
        default: rc = cgra_conv(dev, h, k, b, n, out, outputs); break;
        }
        require(rc == CGRA_OK, cgra_strerror(rc));
        require(memcmp(out, ref, outputs * sizeof(*out)) == 0 && out[outputs] == 12345,
                "scalar comparison or output canary failed");
        cgra_stats_t stats;
        cgra_get_stats(dev, &stats);
        require(stats.retries == 0, "unexpected retry");
        printf("%s,%d,%zu,%zu,%zu,4,4,1,%lu,%lu,%lu\n", names[kind], version,
               n, m, k, stats.transactions, stats.tx_bytes, stats.rx_bytes);
        cgra_close(dev);
    }
    return ferror(stdout) ? 1 : 0;
}
