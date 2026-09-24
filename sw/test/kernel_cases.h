/* Shared workload: software regression and real host serial traces for RTL.
 * Expected results come from scalar arithmetic, independent of the emulator. */
#ifndef CGRA_KERNEL_CASES_H
#define CGRA_KERNEL_CASES_H
#include "compile.h"
#include <stdio.h>
#include <string.h>

#define KCASE(condition) do { if (!(condition)) { \
    fprintf(stderr, "kernel check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; } } while (0)

static int kernel_cases(cgra_t *dev)
{
    enum { N = 19, M = 5, C = 7 };
    int16_t a[N], b[N], ref[N], out[N + 1];
    const int ops[] = {CGRA_OP_ADD, CGRA_OP_SUB, CGRA_OP_MUL, CGRA_OP_MIN, CGRA_OP_MAX};
    char err[256];
    for (int i = 0; i < N; i++) {
        a[i] = (int16_t)(i % 2 ? 32000 - i * 101 : -30000 + i * 97);
        b[i] = (int16_t)(i * 5 - 41);
    }
    out[N] = 12345;
    for (size_t op = 0; op < sizeof(ops) / sizeof(ops[0]); op++) {
        for (int i = 0; i < N; i++) {
            switch (ops[op]) {
            case CGRA_OP_ADD: ref[i] = (int16_t)(a[i] + b[i]); break;
            case CGRA_OP_SUB: ref[i] = (int16_t)(a[i] - b[i]); break;
            case CGRA_OP_MUL: ref[i] = (int16_t)((int32_t)a[i] * b[i]); break;
            case CGRA_OP_MIN: ref[i] = a[i] < b[i] ? a[i] : b[i]; break;
            default: ref[i] = a[i] > b[i] ? a[i] : b[i]; break;
            }
        }
        KCASE(cgra_vec_binop(dev, (enum cgra_op)ops[op], a, b, out, N) == CGRA_OK);
        KCASE(memcmp(out, ref, sizeof(ref)) == 0 && out[N] == 12345);
    }
    for (int i = 0; i < N; i++) ref[i] = a[i] < 0 ? 0 : a[i];
    KCASE(cgra_vec_relu(dev, a, out, N) == CGRA_OK);
    KCASE(memcmp(out, ref, sizeof(ref)) == 0);
    for (int i = 0; i < N; i++) ref[i] = (int16_t)(a[i] + 1234);
    KCASE(cgra_vec_addi(dev, a, 1234, out, N) == CGRA_OK);
    KCASE(memcmp(out, ref, sizeof(ref)) == 0);
    for (int i = 0; i < N; i++) ref[i] = (int16_t)((int32_t)a[i] * -7);
    KCASE(cgra_vec_muli(dev, a, -7, out, N) == CGRA_OK);
    KCASE(memcmp(out, ref, sizeof(ref)) == 0 && out[N] == 12345);
    int16_t dot = 0, dot_ref = 0;
    for (int i = 0; i < N; i++) dot_ref = (int16_t)(dot_ref + (int32_t)a[i] * b[i]);
    KCASE(cgra_dot(dev, a, b, N, &dot) == CGRA_OK && dot == dot_ref);

    dsl_ctx ctx;
    dsl_init(&ctx);
    KCASE(dsl_load_defaults(&ctx, err, sizeof(err)) == 0);
    const char *modes[] = {"add", "mul", "relu", "dot"};
    for (size_t k = 0; k < sizeof(modes) / sizeof(modes[0]); k++) {
        dsl_mode *mode = dsl_find_mode(&ctx, modes[k]);
        KCASE(mode != NULL);
        KCASE(mode_run(dev, mode, a, b, N, 0, 0, out, N, err, sizeof(err)) == (k == 3 ? 1 : N));
        for (int i = 0; i < N; i++) {
            if (k == 0) ref[i] = (int16_t)(a[i] + b[i]);
            if (k == 1) ref[i] = (int16_t)((int32_t)a[i] * b[i]);
            if (k == 2) ref[i] = a[i] < 0 ? 0 : a[i];
        }
        KCASE(k == 3 ? out[0] == dot_ref : memcmp(out, ref, sizeof(ref)) == 0);
    }

    int16_t matrix[M * C], weights[C], result[M];
    for (int i = 0; i < M * C; i++) matrix[i] = (int16_t)(i * 97 - 1300);
    for (int c = 0; c < C; c++) weights[c] = (int16_t)(c * 5 - 12);
    for (int r = 0; r < M; r++) {
        int32_t sum = 0;
        for (int c = 0; c < C; c++) sum += matrix[r * C + c] * weights[c];
        ref[r] = (int16_t)sum;
    }
    KCASE(matvec_run(dev, matrix, M, C, weights, result, err, sizeof(err)) == CGRA_OK);
    KCASE(memcmp(result, ref, sizeof(result)) == 0);

    const int scans[] = {CGRA_OP_ADD, CGRA_OP_MUL, CGRA_OP_MIN, CGRA_OP_MAX};
    for (size_t k = 0; k < sizeof(scans) / sizeof(scans[0]); k++) {
        ref[0] = a[0];
        for (int i = 1; i < N; i++) {
            switch (scans[k]) {
            case CGRA_OP_ADD: ref[i] = (int16_t)(ref[i - 1] + a[i]); break;
            case CGRA_OP_MUL: ref[i] = (int16_t)((int32_t)ref[i - 1] * a[i]); break;
            case CGRA_OP_MIN: ref[i] = ref[i - 1] < a[i] ? ref[i - 1] : a[i]; break;
            default: ref[i] = ref[i - 1] > a[i] ? ref[i - 1] : a[i]; break;
            }
        }
        KCASE(scan_run(dev, a, N, scans[k], out, err, sizeof(err)) == N);
        KCASE(memcmp(out, ref, sizeof(ref)) == 0 && out[N] == 12345);
    }
    const int16_t h[3] = {-3, 11, 5}, x[5] = {2000, -3000, 700, -900, 1234};
    for (int i = 0; i < 7; i++) {
        int32_t sum = 0;
        for (int k = 0; k < 3; k++) if (i >= k && i - k < 5) sum += h[k] * x[i - k];
        ref[i] = (int16_t)sum;
    }
    KCASE(conv_run(dev, h, 3, x, 5, out, N, err, sizeof(err)) == 7);
    KCASE(memcmp(out, ref, 7 * sizeof(*out)) == 0);
    return 0;
}
#undef KCASE
#endif
