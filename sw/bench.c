#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cli.h"
#include "compile.h"
#include "paths.h"
#include "bench_ref.h"

/* One benchmark measurement of a mode. */
typedef struct {
    double ms_total, ms_iter, elems_per_s;
    cgra_stats_t st;
} bench_result;

/* Escape controls/quotes; preserve valid UTF-8 and encode invalid bytes so
 * even a byte-oriented configuration cannot produce malformed JSON. */
static void json_string(FILE *out, const char *value)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
        else if (*p < 32) fprintf(out, "\\u%04x", *p);
        else if (*p >= 128) {
            unsigned count = *p >= 0xC2 && *p <= 0xDF ? 2u :
                             *p >= 0xE0 && *p <= 0xEF ? 3u :
                             *p >= 0xF0 && *p <= 0xF4 ? 4u : 0u;
            unsigned k = 1;
            for (; k < count; k++)
                if (p[k] < 0x80 || p[k] > 0xBF) break;
            int valid = count && k == count &&
                !(*p == 0xE0 && p[1] < 0xA0) && !(*p == 0xED && p[1] >= 0xA0) &&
                !(*p == 0xF0 && p[1] < 0x90) && !(*p == 0xF4 && p[1] >= 0x90);
            if (valid) { fwrite(p, 1, count, out); p += count - 1; }
            else fprintf(out, "\\u%04x", *p);
        }
        else fputc(*p, out);
    }
    fputc('"', out);
}

/* Validate before measurement. Reference generation and comparison are outside
 * timed intervals; every measured invocation must produce the expected result.
 * Return 0 success, 1 unsupported reference, -1 execution/validation failure. */
static int bench_mode(cgra_t *dev, const dsl_mode *m, int size, int repeat,
                      const int16_t *a, const int16_t *b, int16_t *out,
                      bench_result *r, char *err, size_t errsz)
{
    memset(r, 0, sizeof(*r));
    cgra_info_t geometry;
    uint32_t cfg[CGRA_MAX_PE];
    int16_t expected[MAX_VEC];
    int rc = cgra_get_info(dev, &geometry);
    if (rc != CGRA_OK) { snprintf(err, errsz, "%s", cgra_strerror(rc)); return -1; }
    if (mode_compile_for_geometry(m, 0, 0, geometry, cfg, CGRA_MAX_PE, err, errsz)) return -1;
    int count = bench_reference(m, geometry, a, b, (size_t)size, expected, err, errsz);
    if (count < 0) return 1;
    cgra_reset_stats(dev);
    double total_ns = 0;
    for (int i = 0; i < repeat; i++) {
        struct timespec t0, t1;
        if (clock_gettime(CLOCK_MONOTONIC, &t0)) {
            snprintf(err, errsz, "cannot read monotonic clock"); return -1;
        }
        rc = mode_run(dev, m, a, b, (size_t)size, 0, 0, out, MAX_VEC, err, errsz);
        if (clock_gettime(CLOCK_MONOTONIC, &t1)) {
            snprintf(err, errsz, "cannot read monotonic clock"); return -1;
        }
        if (rc < 0) return -1;
        total_ns += (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
        if (rc != count) {
            snprintf(err, errsz, "output count %d differs from scalar reference %d", rc, count);
            return -1;
        }
        for (int j = 0; j < count; j++)
            if (out[j] != expected[j]) {
                snprintf(err, errsz, "iteration %d output %d: got %d, expected %d", i + 1, j, out[j], expected[j]);
                return -1;
            }
    }
    if (total_ns <= 0 || !isfinite(total_ns)) {
        snprintf(err, errsz, "invalid measured duration"); return -1;
    }
    r->ms_total = total_ns / 1e6;
    r->ms_iter = r->ms_total / repeat;
    r->elems_per_s = (double)size * repeat / (total_ns / 1e9);
    cgra_get_stats(dev, &r->st);
    return 0;
}

static void bench_fill(int16_t *a, int16_t *b, int size)
{
    /* Deterministic signs, wraparound and shift amounts beyond 15. */
    static const int16_t edges[] = {INT16_MIN, INT16_MAX, -1, 0, 1, -32767, 16, -17};
    for (int i = 0; i < size; i++) {
        a[i] = i < 8 ? edges[i] : (int16_t)(i * 3 - 19);
        b[i] = i < 8 ? edges[7 - i] : (int16_t)(i - 7);
    }
}

/* A completed report includes failures and skips. Opening/configuration errors
 * before the report starts preserve an existing -o destination. */
static int bench_report(cli_context *inv, dsl_ctx *d, const char *devname, const dsl_mode *single,
                        int size, int repeat)
{
    static int16_t a[MAX_VEC], b[MAX_VEC], out[MAX_VEC];
    bench_fill(a, b, size);
    char err[DSL_VAL];
    connection_info connection;
    cgra_t *dev = cli_open_device_resolved(d, devname, err, sizeof(err), &connection);
    if (!dev) { fprintf(stderr, "cgra: %s\n", err); return 1; }
    cgra_info_t geometry;
    int id_rc = cgra_get_info(dev, &geometry);
    if (id_rc != CGRA_OK) {
        fprintf(stderr, "cgra: cannot identify benchmark device: %s\n", cgra_strerror(id_rc));
        cgra_close(dev); return 1;
    }
    int json = inv->format && !strcmp(inv->format, "json");
    FILE *o = inv->out ? inv->out : stdout;
    if (json) {
        fprintf(o, "{\"schema_version\":1,\"size\":%d,\"repeat\":%d,\"device\":{\"port\":", size, repeat);
        json_string(o, connection.port);
        fputs(",\"backend\":", o);
        json_string(o, !strcmp(connection.port, "sim") || !strncmp(connection.port, "sim:", 4) ? "emulator" : "serial");
        fprintf(o, ",\"rows\":%u,\"cols\":%u,\"data_width\":%u,\"protocol_version\":%u,"
                   "\"baud\":%d,\"timeout_ms\":%d},\"compiler\":",
                geometry.rows, geometry.cols, geometry.data_w, geometry.version, connection.baud, connection.timeout_ms);
        json_string(o, __VERSION__);
        fputs(",\"workload\":\"deterministic-edges-v1\",\"results\":[\n", o);
    } else fprintf(o, "# CGRA benchmark: %ux%u protocol v%u on %s; %d elems x %d iterations; scalar checks outside timing\n",
                   geometry.rows, geometry.cols, geometry.version, connection.port, size, repeat);

    int passed = 0, failed = 0, skipped = 0;
    unsigned long tx = 0, rx = 0, retries = 0;
    double total_ms = 0;
    int modes = single ? 1 : d->nmode;
    for (int i = 0; i < modes; i++) {
        const dsl_mode *m = single ? single : &d->mode[i];
        const char *pattern = m->pattern[0] ? m->pattern : "diagonal";
        bench_result result;
        err[0] = 0;
        int rc = bench_mode(dev, m, size, repeat, a, b, out, &result, err, sizeof(err));
        const char *status = rc == 0 ? "passed" : rc > 0 ? "skipped" : "failed";
        if (rc == 0) {
            passed++;
            tx += result.st.tx_bytes; rx += result.st.rx_bytes; retries += result.st.retries;
            total_ms += result.ms_total;
        } else if (rc > 0) skipped++;
        else { failed++; fprintf(stderr, "cgra: benchmark %s failed: %s\n", m->name, err); }
        if (json) {
            fprintf(o, "%s{\"mode\":", i ? ",\n" : ""); json_string(o, m->name);
            fputs(",\"pattern\":", o); json_string(o, pattern);
            fputs(",\"status\":", o); json_string(o, status);
            fputs(",\"mapping\":{\"op\":", o); json_string(o, m->op);
            fputs(",\"a\":", o); json_string(o, m->a);
            fputs(",\"b\":", o); json_string(o, m->b);
            fputs(",\"steps\":", o); json_string(o, m->steps);
            fputs(",\"reset\":", o); json_string(o, m->reset);
            fputs(",\"out\":", o); json_string(o, m->out);
            fputc('}', o);
            if (rc == 0)
                fprintf(o, ",\"verification\":\"scalar\",\"elems\":%d,\"iters\":%d,"
                           "\"ms_per_iter\":%.9g,\"elems_per_s\":%.9g,"
                           "\"transactions\":%lu,\"tx_bytes\":%lu,\"rx_bytes\":%lu,\"retries\":%lu",
                        size, repeat, result.ms_iter, result.elems_per_s, result.st.transactions,
                        result.st.tx_bytes, result.st.rx_bytes, result.st.retries);
            else { fputs(",\"reason\":", o); json_string(o, err); }
            fputc('}', o);
        } else if (rc == 0)
            fprintf(o, "%-16s %-8s passed  %.4f ms/iter, %.0f elems/s; tx=%lu rx=%lu retries=%lu\n",
                    m->name, pattern, result.ms_iter, result.elems_per_s,
                    result.st.tx_bytes, result.st.rx_bytes, result.st.retries);
        else fprintf(o, "%-16s %-8s %s: %s\n", m->name, pattern, status, err);
    }
    cgra_close(dev);
    int unsuccessful = failed || !passed || (single && skipped);
    if (single && skipped) fprintf(stderr, "cgra: requested benchmark has no scalar reference\n");
    else if (!passed) fprintf(stderr, "cgra: no benchmark completed successfully\n");
    if (json)
        fprintf(o, "\n],\"modes\":%d,\"failed\":%d,\"skipped\":%d,\"ok\":%s,"
                   "\"total_ms\":%.9g,\"total_tx\":%lu,\"total_rx\":%lu,\"total_retries\":%lu}\n",
                passed, failed, skipped, unsuccessful ? "false" : "true", total_ms, tx, rx, retries);
    else fprintf(o, "# %d passed, %d failed, %d skipped; successful measurements: %.4f ms, %lu tx, %lu rx, %lu retries\n",
                 passed, failed, skipped, total_ms, tx, rx, retries);
    inv->report_ready = 1;
    return unsuccessful ? 1 : 0;
}

int cli_cmd_bench(cli_context *inv, dsl_ctx *d, const char *devname, const char *modename,
                     int size, int repeat)
{
    const char *name = modename ? modename : "add";
    dsl_mode *m = dsl_find_mode(d, name);
    if (!m) { fprintf(stderr, "cgra: unknown mode '%s'\n", name); return 1; }
    return bench_report(inv, d, devname, m, size ? size : 256, repeat ? repeat : 100);
}

int cli_cmd_benchall(cli_context *inv, dsl_ctx *d, const char *devname, int size, int repeat)
{
    return bench_report(inv, d, devname, NULL, size ? size : 1024, repeat ? repeat : 50);
}
