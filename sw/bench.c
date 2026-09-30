#define _GNU_SOURCE
#include <math.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/utsname.h>
#include "build_provenance.h"
#include "cli.h"
#include "compile.h"
#include "paths.h"
#include "bench_ref.h"

/* One benchmark measurement of a mode. */
typedef struct {
    double ms_total, ms_iter, elems_per_s;
    double prepare_ms, verify_ms, median_ms, p95_ms, cpu_median_ms, cpu_p95_ms;
    double *samples, *cpu_samples;
    int completed, outputs;
    cgra_stats_t st;
    cgra_profile_t profile;
    uint32_t cfg[CGRA_MAX_PE];
    unsigned ncfg;
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

/* Workload parameters are fixed across geometries: matvec has size rows
 * and 16 columns; convolution has size inputs and five coefficients. */
static int execute(cgra_t *dev, const dsl_mode *m, int size,
                   const int16_t *a, const int16_t *b, int16_t *out,
                   char *err, size_t errsz)
{
    int rc;
    if (!strcmp(m->pattern, "systolic"))
        rc = cgra_matvec(dev, a, (size_t)size, 16, b, out, (size_t)size);
    else if (!strcmp(m->pattern, "conv"))
        rc = cgra_conv(dev, b, 5, a, (size_t)size, out, (size_t)size + 4);
    else if (!strcmp(m->pattern, "scan"))
        rc = cgra_scan(dev, CGRA_OP_ADD, a, (size_t)size, out, (size_t)size);
    else return mode_run(dev, m, a, b, (size_t)size, 0, 0, out, MAX_VEC + 4, err, errsz);
    if (rc != CGRA_OK) { snprintf(err, errsz, "%s", cgra_strerror(rc)); return -1; }
    return size + (!strcmp(m->pattern, "conv") ? 4 : 0);
}

static double now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return NAN;
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static int compare_outputs(int got, int count, const int16_t *out, const int16_t *expected,
                           char *err, size_t errsz)
{
    if (got != count) {
        snprintf(err, errsz, "output count %d differs from scalar reference %d", got, count);
        return -1;
    }
    for (int j = 0; j < count; j++)
        if (out[j] != expected[j]) {
            snprintf(err, errsz, "output %d: got %d, expected %d", j, out[j], expected[j]);
            return -1;
        }
    return 0;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void statistics(const double *samples, double *scratch, int n, double *median, double *p95)
{
    size_t count = (size_t)n;
    memcpy(scratch, samples, count * sizeof(double));
    qsort(scratch, count, sizeof(double), compare_double);
    *median = (scratch[(count - 1) / 2] + scratch[count / 2]) / 2;
    *p95 = scratch[(count * 95 + 99) / 100 - 1]; /* nearest-rank p95 */
}

/* Independent scalar reference and comparisons are outside API timing.
 * One checked warm-up precedes measured calls; wire statistics exclude it. */
static int bench_mode(cgra_t *dev, const dsl_mode *m, int size, int repeat,
                      const int16_t *a, const int16_t *b, int16_t *out,
                      bench_result *r, char *err, size_t errsz)
{
    memset(r, 0, sizeof(*r));
    double start = now_ms();
    cgra_info_t geometry;
    uint32_t cfg[CGRA_MAX_PE];
    int16_t expected[MAX_VEC + 4], cpu_output[MAX_VEC + 4];
    int rc = cgra_get_info(dev, &geometry);
    if (rc != CGRA_OK) { snprintf(err, errsz, "%s", cgra_strerror(rc)); return -1; }
    if (mode_compile_for_geometry(m, 0, 0, geometry, cfg, CGRA_MAX_PE, err, errsz)) return -1;
    r->ncfg = (unsigned)geometry.rows * geometry.cols;
    memcpy(r->cfg, cfg, r->ncfg * sizeof(*cfg));
    int count = bench_reference(m, geometry, a, b, (size_t)size, expected, err, errsz);
    if (count < 0) return 1;
    r->outputs = count;
    if ((size_t)repeat > SIZE_MAX / (3 * sizeof(double))) {
        snprintf(err, errsz, "too many timing samples"); return -1;
    }
    r->samples = malloc((size_t)repeat * 3 * sizeof(double));
    if (!r->samples) { snprintf(err, errsz, "cannot allocate timing samples"); return -1; }
    r->cpu_samples = r->samples + repeat;
    double *scratch = r->cpu_samples + repeat;
    r->prepare_ms = now_ms() - start;
    rc = execute(dev, m, size, a, b, out, err, errsz);
    if (rc < 0 || compare_outputs(rc, count, out, expected, err, errsz)) return -1;
    /* Warm the scalar implementation too. Its outputs are consumed below,
     * including for LTO builds, so its computation cannot be discarded. */
    rc = bench_reference(m, geometry, a, b, (size_t)size, cpu_output, err, errsz);
    if (compare_outputs(rc, count, cpu_output, expected, err, errsz)) return -1;
    cgra_reset_stats(dev);
    for (int i = 0; i < repeat; i++) {
        cgra_set_profiling(dev, 1);
        start = now_ms();
        rc = execute(dev, m, size, a, b, out, err, errsz);
        r->samples[i] = now_ms() - start;
        cgra_profile_t profile;
        cgra_get_profile(dev, &profile);
        cgra_set_profiling(dev, 0);
        r->profile.configuration_io_ns += profile.configuration_io_ns;
        r->profile.execution_io_ns += profile.execution_io_ns;
        r->profile.configuration_transactions += profile.configuration_transactions;
        r->profile.requested_steps += profile.requested_steps;
        cgra_get_stats(dev, &r->st);
        if (rc < 0) return -1;
        if (!profile.valid) { snprintf(err, errsz, "profiling clock failed"); return -1; }
        double verify_start = now_ms();
        if (compare_outputs(rc, count, out, expected, err, errsz)) return -1;
        r->verify_ms += now_ms() - verify_start;
        start = now_ms();
        rc = bench_reference(m, geometry, a, b, (size_t)size, cpu_output, err, errsz);
        r->cpu_samples[i] = now_ms() - start;
        verify_start = now_ms();
        if (compare_outputs(rc, count, cpu_output, expected, err, errsz)) return -1;
        r->verify_ms += now_ms() - verify_start;
        if (!isfinite(r->samples[i]) || r->samples[i] < 0 ||
            !isfinite(r->cpu_samples[i]) || r->cpu_samples[i] < 0) {
            snprintf(err, errsz, "invalid monotonic clock duration"); return -1;
        }
        r->ms_total += r->samples[i];
        r->completed++;
    }
    if (r->ms_total <= 0 || !isfinite(r->prepare_ms) || !isfinite(r->verify_ms)) {
        snprintf(err, errsz, "invalid measured duration"); return -1;
    }
    r->ms_iter = r->ms_total / repeat;
    r->elems_per_s = (double)size * repeat * 1000 / r->ms_total;
    statistics(r->samples, scratch, repeat, &r->median_ms, &r->p95_ms);
    statistics(r->cpu_samples, scratch, repeat, &r->cpu_median_ms, &r->cpu_p95_ms);
    return 0;
}

/* Useful scalar multiplies/additions, excluding routing, padding and an
 * initial zero addition. Unknown mappings have no invented count. */
static int useful_arithmetic(const dsl_mode *m, int size)
{
    if (!strcmp(m->pattern, "systolic")) return size * 31;
    if (!strcmp(m->pattern, "conv")) return size * 10 - (size + 4);
    if (!strcmp(m->pattern, "scan")) return size - 1;
    if (!strcmp(m->pattern, "custom")) return size; /* recognized ADD only */
    if (!m->pattern[0] || !strcmp(m->pattern, "diagonal")) {
        if (!m->op[0] || !strcmp(m->op, "add") || !strcmp(m->op, "sub") || !strcmp(m->op, "mul"))
            return size;
    }
    return -1;
}

static void bench_fill(int16_t *a, int16_t *b, int size)
{
    static const int16_t edges[] = {INT16_MIN, INT16_MAX, -1, 0, 1, -32767, 16, -17};
    for (int i = 0; i < size * 16; i++) {
        uint16_t bits = (uint16_t)(i * 3 - 19);
        a[i] = i < 8 ? edges[i] : (int16_t)(bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536);
    }
    for (int i = 0; i < (size < 16 ? 16 : size); i++)
        b[i] = i < 8 ? edges[7 - i] : (int16_t)(i - 7);
}

static void json_samples(FILE *out, const double *samples, int count)
{
    fputc('[', out);
    for (int i = 0; i < count; i++) fprintf(out, "%s%.9g", i ? "," : "", samples[i]);
    fputc(']', out);
}

/* A completed report includes failures and skips. Opening/configuration errors
 * before the report starts preserve an existing -o destination. */
static int bench_report(cli_context *inv, dsl_ctx *d, const char *devname, const dsl_mode *single,
                        int size, int repeat)
{
    static int16_t a[MAX_VEC * 16], b[MAX_VEC], out[MAX_VEC + 4];
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
        fprintf(o, "{\"schema_version\":2,\"size\":%d,\"repeat\":%d,\"device\":{\"port\":", size, repeat);
        json_string(o, connection.port);
        fputs(",\"backend\":", o);
        json_string(o, !strcmp(connection.port, "sim") || !strncmp(connection.port, "sim:", 4) ? "emulator" : "serial");
        fprintf(o, ",\"rows\":%u,\"cols\":%u,\"data_width\":%u,\"protocol_version\":%u,"
                   "\"baud\":%d,\"timeout_ms\":%d},\"compiler\":",
                geometry.rows, geometry.cols, geometry.data_w, geometry.version, connection.baud, connection.timeout_ms);
        json_string(o, __VERSION__);
        fputs(",\"build\":", o); fputs(CGRA_BUILD_JSON, o);
        struct utsname platform;
        fputs(",\"platform\":", o);
        json_string(o, uname(&platform) == 0 ? platform.sysname : "unknown");
        fputs(",\"machine\":", o);
        json_string(o, uname(&platform) == 0 ? platform.machine : "unknown");
        fputs(",\"bitstream_id\":", o);
        const char *bitstream = getenv("CGRA_BITSTREAM_ID");
        if (bitstream && *bitstream) json_string(o, bitstream); else fputs("null", o);
        fputs(",\"bitstream_id_source\":\"operator environment, not device attestation\","
              "\"warmup\":1,\"workload\":\"deterministic-edges-v2\","
              "\"transport_profiling\":true,\"cpu_baseline\":\"scalar reference wall time, includes dispatch\",\"timing_scope\":\"host API wall time including configuration and communication; not FPGA throughput\","
              "\"results\":[\n", o);
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
            fprintf(o, ",\"completed_iterations\":%d,\"outputs\":%d,\"parameters\":{\"matrix_cols\":%d,\"filter_length\":%d}",
                    result.completed, result.outputs, !strcmp(pattern, "systolic") ? 16 : 0,
                    !strcmp(pattern, "conv") ? 5 : 0);
            fputs(",\"samples_ms\":", o); json_samples(o, result.samples, result.completed);
            fputs(",\"cpu_samples_ms\":", o); json_samples(o, result.cpu_samples, result.completed);
            if (rc == 0)
                fprintf(o, ",\"median_ms\":%.9g,\"p95_ms\":%.9g,"
                           "\"cpu_median_ms\":%.9g,\"cpu_p95_ms\":%.9g,"
                           "\"prepare_ms\":%.9g,\"verify_ms\":%.9g,"
                           "\"configuration_io_ms\":%.9g,\"execution_io_ms\":%.9g,"
                           "\"host_api_other_ms\":%.9g,\"configuration_transactions\":%" PRIu64 ","
                           "\"requested_steps\":%" PRIu64,
                        result.median_ms, result.p95_ms, result.cpu_median_ms, result.cpu_p95_ms,
                        result.prepare_ms, result.verify_ms,
                        (double)result.profile.configuration_io_ns / 1e6,
                        (double)result.profile.execution_io_ns / 1e6,
                        result.ms_total - (double)(result.profile.configuration_io_ns + result.profile.execution_io_ns) / 1e6,
                        result.profile.configuration_transactions, result.profile.requested_steps);
            fputs(",\"compiled_template_words\":[", o);
            for (unsigned k = 0; k < result.ncfg; k++) fprintf(o, "%s%" PRIu32, k ? "," : "", result.cfg[k]);
            fputs("],\"useful_arithmetic_ops_per_iteration\":", o);
            int ops = rc == 0 ? useful_arithmetic(m, size) : -1;
            if (ops < 0) fputs("null", o); else fprintf(o, "%d", ops);
            fputc('}', o);
        } else if (rc == 0)
            fprintf(o, "%-16s %-8s passed  median=%.4f p95=%.4f CPU-median=%.4f ms; tx=%lu rx=%lu retries=%lu\n",
                    m->name, pattern, result.median_ms, result.p95_ms, result.cpu_median_ms,
                    result.st.tx_bytes, result.st.rx_bytes, result.st.retries);
        else fprintf(o, "%-16s %-8s %s: %s\n", m->name, pattern, status, err);
        free(result.samples);
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
