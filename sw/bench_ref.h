#ifndef CGRA_BENCH_REF_H
#define CGRA_BENCH_REF_H

#include "dsl.h"

/* Independent scalar reference for stateless diagonal modes, PE(0,0)
 * reductions, known custom ADD routing, matvec, scan and convolution.
 * Benchmark workload: systolic uses n rows, 16 columns (a[n*16], b[16]);
 * conv uses a[n], b[5], out[n+4]; other modes use a[n], b[n], out[n].
 * Call after mode_compile_for_geometry validation. Returns output count or
 * -1 when no scalar reference is available; reason describes that limitation.
 * Does not call the compiler, emulator, transport or mode_run. */
int bench_reference(const dsl_mode *mode, cgra_info_t geometry,
                    const int16_t *a, const int16_t *b, size_t n,
                    int16_t *out, char *reason, size_t reason_size);

#endif
