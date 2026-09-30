/* One reproducible API point. GNU ld wrappers observe project allocations and
 * complete emulator request frames; they do not model UART/FPGA time. */
#define _POSIX_C_SOURCE 200809L
#include "cgra.h"
#include "emu.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t s);
void __real_free(void *p);
void __real_emu_feed(cgra_emu_t *e, const uint8_t *p, size_t n);
void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n, size_t s);
void __wrap_free(void *p);
void __wrap_emu_feed(cgra_emu_t *e, const uint8_t *p, size_t n);

static int measuring;
static size_t allocated, live_bytes, peak_live, largest;
static unsigned long logical_steps;
static struct { void *ptr; size_t size; } allocations[128];

static void fail(const char *message)
{
    fprintf(stderr, "geometry experiment: %s\n", message);
    exit(1);
}

static void *track(void *p, size_t size)
{
    if (measuring && p) {
        size_t i;
        for (i = 0; i < 128 && allocations[i].ptr; ++i) {}
        if (i == 128) fail("allocation observer capacity exceeded");
        allocations[i].ptr = p; allocations[i].size = size;
        allocated += size; live_bytes += size;
        if (live_bytes > peak_live) peak_live = live_bytes;
        if (size > largest) largest = size;
    }
    return p;
}
void *__wrap_malloc(size_t n) { return track(__real_malloc(n), n); }
void *__wrap_calloc(size_t n, size_t s)
{
    if (s && n > SIZE_MAX / s) fail("allocation overflow");
    return track(__real_calloc(n, s), n * s);
}
void __wrap_free(void *p)
{
    if (p) for (size_t i = 0; i < 128; ++i) if (allocations[i].ptr == p) {
        live_bytes -= allocations[i].size; allocations[i].ptr = NULL; break;
    }
    __real_free(p);
}
void __wrap_emu_feed(cgra_emu_t *e, const uint8_t *p, size_t n)
{
    /* The sim transport submits each complete request in one feed. */
    if (measuring && n >= 2 && (p[0] == 4 || p[0] == 7)) logical_steps += p[1];
    __real_emu_feed(e, p, n);
}
static uint64_t clock_ns(clockid_t clock)
{
    struct timespec t;
    if (clock_gettime(clock, &t)) fail("clock_gettime");
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

int main(int argc, char **argv)
{
    if (argc != 6) fail("usage: point sim:RxC[:v2] add|scan|matvec|conv n samples warmup");
    const char *workload = argv[2];
    size_t n = (size_t)strtoul(argv[3], NULL, 10), k = 5;
    int samples = atoi(argv[4]), warmup = atoi(argv[5]);
    if (!n || n > 4096 || samples < 1 || samples > 100 || warmup < 0 || warmup > 10) fail("parameters");
    int kind = !strcmp(workload,"add") ? 0 : !strcmp(workload,"scan") ? 1 :
               !strcmp(workload,"matvec") ? 2 : !strcmp(workload,"conv") ? 3 : -1;
    if (kind < 0) fail("workload");
    size_t count = kind == 3 ? n + k - 1 : n;
    int16_t *a = malloc(n * sizeof(*a)), *b = malloc(n * sizeof(*b));
    int16_t *out = malloc((count + 1) * sizeof(*out));
    uint16_t *ref = calloc(count, sizeof(*ref));
    int16_t *matrix = kind == 2 ? malloc(n*n*sizeof(*matrix)) : NULL;
    int16_t h[] = {1, -2, 3, -4, 5};
    if (!a || !b || !out || !ref || (kind == 2 && !matrix)) fail("workload allocation");
    for (size_t i = 0; i < n; ++i) {
        a[i] = (int16_t)(i % 7 + 1);
        b[i] = (int16_t)(kind == 2 ? i + 1 : i % 5 + 1);
    }
    for (size_t i = 0; matrix && i < n*n; ++i) matrix[i] = (int16_t)(i % 7 + 1);
    if (kind == 0) for (size_t i = 0; i < n; ++i) ref[i] = (uint16_t)(a[i] + b[i]);
    if (kind == 1) for (size_t i = 0; i < n; ++i) ref[i] = (uint16_t)(a[i] + (i ? ref[i-1] : 0));
    if (kind == 2) for (size_t r = 0; r < n; ++r) for (size_t c = 0; c < n; ++c)
        ref[r] = (uint16_t)(ref[r] + (uint32_t)(uint16_t)matrix[r*n+c] * (uint16_t)b[c]);
    if (kind == 3) for (size_t j = 0; j < n; ++j) for (size_t t = 0; t < k; ++t)
        ref[j+t] = (uint16_t)(ref[j+t] + (uint32_t)(uint16_t)b[j] * (uint16_t)h[t]);
    cgra_t *dev = cgra_open(argv[1], 115200);
    cgra_info_t geometry;
    if (!dev || cgra_get_info(dev, &geometry)) fail("identify");
    printf("{\"schema_version\":1,\"workload\":\"%s\",\"n\":%zu,\"k\":%zu,"
           "\"rows\":%u,\"cols\":%u,\"protocol\":%u,\"warmup\":%d,\"samples\":[",
           workload,n,kind==3?k:0,geometry.rows,geometry.cols,geometry.version,warmup);
    for (int iteration = -warmup; iteration < samples; ++iteration) {
        out[count] = 12345;
        allocated = peak_live = largest = 0;
        logical_steps = 0;
        cgra_reset_stats(dev); /* Initial ID excluded; CFG included on every call. */
        measuring = 1;
        uint64_t cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID), wall = clock_ns(CLOCK_MONOTONIC);
        int rc = kind == 0 ? cgra_vec_add(dev,a,b,out,n) :
                 kind == 1 ? cgra_scan(dev,CGRA_OP_ADD,a,n,out,count) :
                 kind == 2 ? cgra_matvec(dev,matrix,n,n,b,out,count) : cgra_conv(dev,h,k,b,n,out,count);
        wall = clock_ns(CLOCK_MONOTONIC) - wall;
        cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu;
        measuring = 0;
        if (rc || out[count] != 12345 || live_bytes) fail("API/capacity/allocation check");
        for (size_t i = 0; i < count; ++i) if ((uint16_t)out[i] != ref[i]) fail("independent scalar comparison");
        cgra_stats_t stats; cgra_get_stats(dev,&stats);
        struct rusage usage; if (getrusage(RUSAGE_SELF, &usage)) fail("RSS");
        if (iteration >= 0) printf("%s{\"wall_ns\":%"PRIu64",\"process_cpu_ns\":%"PRIu64","
            "\"transactions\":%lu,\"tx_bytes\":%lu,\"rx_bytes\":%lu,\"logical_steps\":%lu,"
            "\"temporary_heap_requested\":%zu,\"temporary_heap_peak_live\":%zu,"
            "\"largest_temporary_allocation\":%zu,\"process_peak_rss_kib\":%ld}",
            iteration?",":"",wall,cpu,stats.transactions,stats.tx_bytes,stats.rx_bytes,logical_steps,
            allocated,peak_live,largest,usage.ru_maxrss);
    }
    puts("],\"verified\":true}");
    cgra_close(dev); free(matrix); free(ref); free(out); free(b); free(a);
    return ferror(stdout) ? 1 : 0;
}
