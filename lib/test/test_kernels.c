/* Public kernel contracts; this test includes only the installed API. */
#include "cgra.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(cond, message) do { \
    int passed = (cond); \
    printf("%s kernel %d - %s\n", passed ? "ok" : "not ok", ++checks, message); \
    assert(passed); \
} while (0)

static void test_contracts(void)
{
    cgra_t *dev = cgra_open("sim:2x3", 115200);
    CHECK(dev != NULL, "open public contract test device");
    int16_t a[12], before[12], b[12], out[12];
    for (size_t i = 0; i < 12; i++) { a[i] = (int16_t)i; b[i] = 2; out[i] = 123; }
    memcpy(before, a, sizeof(a));
    const size_t huge = (size_t)PTRDIFF_MAX / sizeof(int16_t) + 1;
    CHECK(cgra_vec_add(dev, a, b, a + 1, 6) == CGRA_ERR_ARG &&
          cgra_vec_add(dev, a + 1, b, a, 6) == CGRA_ERR_ARG,
          "vector partial overlap rejected in both directions");
    CHECK(cgra_vec_addi(dev, a, 1, a + 1, 6) == CGRA_ERR_ARG &&
          cgra_scan(dev, CGRA_OP_ADD, a, 6, a + 1, 6) == CGRA_ERR_ARG &&
          cgra_scan(dev, CGRA_OP_ADD, a + 1, 6, a, 6) == CGRA_ERR_ARG,
          "immediate and scan partial overlap rejected");
    CHECK(cgra_matvec(dev, a, 2, 3, b, a, 2) == CGRA_ERR_ARG &&
          cgra_matvec(dev, a, 2, 3, b, b + 1, 2) == CGRA_ERR_ARG,
          "matrix output cannot overwrite A or x");
    CHECK(cgra_conv(dev, a, 2, b, 3, a, 4) == CGRA_ERR_ARG &&
          cgra_conv(dev, a, 2, b, 3, b + 1, 4) == CGRA_ERR_ARG,
          "convolution output cannot overwrite either input");
    CHECK(cgra_matvec(dev, a, 2, 3, b, out, 1) == CGRA_ERR_ARG &&
          cgra_scan(dev, CGRA_OP_ADD, a, 4, out, 3) == CGRA_ERR_ARG &&
          cgra_conv(dev, a, 2, b, 3, out, 3) == CGRA_ERR_ARG,
          "new kernels enforce output capacities");
    CHECK(cgra_matvec(dev, a, huge, 2, b, out, SIZE_MAX) == CGRA_ERR_ARG &&
          cgra_matvec(dev, a, 2, huge / 2 + 1, b, out, 2) == CGRA_ERR_ARG &&
          cgra_conv(dev, a, huge - 1, b, 3, out, SIZE_MAX) == CGRA_ERR_ARG &&
          cgra_conv(dev, a, 1, b, huge / 2, out, SIZE_MAX) == CGRA_ERR_ARG,
          "matrix product, convolution sum and workspace overflow rejected");
    CHECK(cgra_vec_add(dev, a, b, out, huge) == CGRA_ERR_ARG &&
          cgra_dot(dev, a, b, huge, out) == CGRA_ERR_ARG &&
          cgra_scan(dev, CGRA_OP_ADD, a, huge, out, SIZE_MAX) == CGRA_ERR_ARG,
          "vector footprints must fit addressable storage");
    CHECK(cgra_scan(dev, CGRA_OP_SUB, a, 4, out, 4) == CGRA_ERR_ARG &&
          cgra_scan(dev, CGRA_OP_MAC, NULL, 0, NULL, 0) == CGRA_ERR_ARG &&
          cgra_vec_binop(dev, CGRA_OP_ACC, NULL, NULL, NULL, 0) == CGRA_ERR_ARG,
          "op validation also applies to empty input");
    CHECK(cgra_matvec(dev, NULL, 2, 3, b, out, 2) == CGRA_ERR_ARG &&
          cgra_scan(dev, CGRA_OP_ADD, NULL, 1, out, 1) == CGRA_ERR_ARG &&
          cgra_conv(dev, NULL, 1, b, 3, out, 3) == CGRA_ERR_ARG,
          "nonempty input pointers are required");
    cgra_stats_t stats;
    cgra_get_stats(dev, &stats);
    CHECK(stats.transactions == 0 && stats.tx_bytes == 0 && stats.rx_bytes == 0 &&
          memcmp(a, before, sizeof(a)) == 0 && out[0] == 123 && out[11] == 123,
          "argument errors preserve output and perform no ID or compute I/O");

    CHECK(cgra_vec_add(dev, NULL, NULL, NULL, 0) == CGRA_OK &&
          cgra_vec_addi(dev, NULL, 1, NULL, 0) == CGRA_OK &&
          cgra_scan(dev, CGRA_OP_ADD, NULL, 0, NULL, 0) == CGRA_OK,
          "empty vectors and scans accept NULL buffers");
    CHECK(cgra_dot(dev, NULL, NULL, 0, out) == CGRA_OK && out[0] == 0 &&
          cgra_dot(dev, NULL, NULL, 0, NULL) == CGRA_ERR_ARG,
          "empty dot returns zero and still requires its result pointer");
    CHECK(cgra_matvec(dev, NULL, 0, 7, NULL, NULL, 0) == CGRA_OK &&
          cgra_matvec(dev, NULL, 2, 0, NULL, out, 2) == CGRA_OK &&
          out[0] == 0 && out[1] == 0 && out[2] == 123,
          "empty matrix rows are a no-op; zero columns write exactly rows zeros");
    CHECK(cgra_conv(dev, NULL, 0, NULL, 3, NULL, 0) == CGRA_OK &&
          cgra_conv(dev, NULL, 3, NULL, 0, NULL, 0) == CGRA_OK,
          "either empty convolution input yields empty output");
    CHECK(cgra_scan(NULL, CGRA_OP_ADD, NULL, 0, NULL, 0) == CGRA_ERR_ARG &&
          cgra_matvec(NULL, NULL, 0, 0, NULL, NULL, 0) == CGRA_ERR_ARG &&
          cgra_conv(NULL, NULL, 0, NULL, 0, NULL, 0) == CGRA_ERR_ARG,
          "even empty kernels require a handle");
    cgra_get_stats(dev, &stats);
    CHECK(stats.transactions == 0, "empty operations do not discover or modify the device");
    cgra_close(dev);

    errno = 0;
    CHECK(cgra_open(NULL, 115200) == NULL && errno == EINVAL, "NULL device reports EINVAL");
    errno = 0;
    CHECK(cgra_open("sim:0x4", 115200) == NULL && errno == EINVAL, "invalid emulator reports EINVAL");
    errno = 0;
    CHECK(cgra_open("/dev/null", 123) == NULL && errno == EINVAL, "unsupported serial baud reports EINVAL");
    errno = 0;
    CHECK(cgra_open("/dev/null", 115200) == NULL && errno == ENOTTY, "serial open preserves tcgetattr errno");
    CHECK(strstr(cgra_strerror(CGRA_ERR_NOMEM), "memory") != NULL, "workspace allocation has a distinct error");
}

static void test_results(const char *path)
{
    enum { N = 19, M = 5, C = 7, K = 3, L = N + K - 1 };
    cgra_t *dev = cgra_open(path, 115200);
    CHECK(dev != NULL, path);
    int16_t A[M * C], x[N], b[N], ref[L], out[L + 1], inplace[N];
    const int16_t h[K] = {INT16_MIN, 11, -7};
    for (int i = 0; i < M * C; i++) A[i] = (int16_t)(i * 197 - 3100);
    for (int i = 0; i < N; i++) { x[i] = (int16_t)(i % 2 ? 32000 - i * 13 : -31000 + i * 17); b[i] = 9; }
    for (int r = 0; r < M; r++) {
        int16_t sum = 0;
        for (int c = 0; c < C; c++) sum = (int16_t)(sum + (int32_t)A[r * C + c] * x[c]);
        ref[r] = sum;
    }
    out[M] = 12345;
    CHECK(cgra_matvec(dev, A, M, C, x, out, M) == CGRA_OK &&
          memcmp(ref, out, M * sizeof(*out)) == 0 && out[M] == 12345,
          "public matvec matches scalar wrap-around reference with rectangular tails");
    const enum cgra_op ops[] = {CGRA_OP_ADD, CGRA_OP_MUL, CGRA_OP_MIN, CGRA_OP_MAX};
    for (size_t k = 0; k < sizeof(ops) / sizeof(ops[0]); k++) {
        ref[0] = x[0];
        for (int i = 1; i < N; i++) {
            switch (ops[k]) {
            case CGRA_OP_ADD: ref[i] = (int16_t)(ref[i - 1] + x[i]); break;
            case CGRA_OP_MUL: ref[i] = (int16_t)((int32_t)ref[i - 1] * x[i]); break;
            case CGRA_OP_MIN: ref[i] = ref[i - 1] < x[i] ? ref[i - 1] : x[i]; break;
            default: ref[i] = ref[i - 1] > x[i] ? ref[i - 1] : x[i]; break;
            }
        }
        out[N] = 12345;
        CHECK(cgra_scan(dev, ops[k], x, N, out, N) == CGRA_OK &&
              memcmp(ref, out, N * sizeof(*out)) == 0 && out[N] == 12345,
              "public scan matches independent scalar fold and preserves output canary");
        memcpy(inplace, x, sizeof(x));
        CHECK(cgra_scan(dev, ops[k], inplace, N, inplace, N) == CGRA_OK &&
              memcmp(ref, inplace, sizeof(inplace)) == 0, "scan supports exact in-place output");
    }
    for (int i = 0; i < L; i++) {
        int16_t sum = 0;
        for (int j = 0; j < K; j++)
            if (i >= j && i - j < N) sum = (int16_t)(sum + (int32_t)h[j] * x[i - j]);
        ref[i] = sum;
    }
    out[L] = 12345;
    CHECK(cgra_conv(dev, h, K, x, N, out, L) == CGRA_OK &&
          memcmp(out, ref, L * sizeof(*out)) == 0 && out[L] == 12345,
          "public convolution matches scalar reference and preserves output canary");
    for (int i = 0; i < N; i++) ref[i] = (int16_t)(x[i] + b[i]);
    memcpy(inplace, x, sizeof(x));
    CHECK(cgra_vec_add(dev, inplace, b, inplace, N) == CGRA_OK &&
          memcmp(inplace, ref, sizeof(inplace)) == 0, "vector output may equal A");
    memcpy(inplace, b, sizeof(b));
    CHECK(cgra_vec_add(dev, x, inplace, inplace, N) == CGRA_OK &&
          memcmp(inplace, ref, sizeof(inplace)) == 0, "vector output may equal B");
    memcpy(inplace, x, sizeof(x));
    CHECK(cgra_vec_addi(dev, inplace, 9, inplace, N) == CGRA_OK &&
          memcmp(inplace, ref, sizeof(inplace)) == 0, "immediate vector supports in-place output");
    int16_t dot = 0;
    for (int i = 0; i < N; i++) dot = (int16_t)(dot + (int32_t)x[i] * b[i]);
    CHECK(cgra_dot(dev, x, b, N, &x[1]) == CGRA_OK && x[1] == dot,
          "dot may store its result inside a consumed input");
    cgra_close(dev);
}

int main(void)
{
    test_contracts();
    const char *shapes[] = {"1x1", "1x16", "16x1", "2x3", "3x2", "2x8", "8x2", "4x4"};
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++)
        for (int version = 2; version <= 3; version++) {
            char path[32];
            snprintf(path, sizeof(path), "sim:%s%s", shapes[i], version == 2 ? ":v2" : "");
            test_results(path);
        }
    printf("# %d public kernel checks passed\n", checks);
    return 0;
}
