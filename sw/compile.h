/*
 * compile.h — turn a DSL mode into PE configuration words and run it.
 */

#ifndef CGRA_COMPILE_H
#define CGRA_COMPILE_H

#include <stddef.h>
#include "cgra.h"
#include "dsl.h"

/* Compile a mode into 16 config words (for inspection / `cgra show`).
 * imm_override replaces the immediate of const operands when has_imm. */
int mode_compile(const dsl_mode *m, long imm_override, int has_imm_override,
                 uint32_t cfg[CGRA_NUM_PE], char *err, size_t errsz);
/* Compile for an explicit supported geometry; capacity is measured in words.
 * Custom coordinates/taps are checked against these dimensions. */
int mode_compile_for_geometry(const dsl_mode *m, long imm_override, int has_imm_override,
                              cgra_info_t geometry, uint32_t *cfg, size_t capacity,
                              char *err, size_t errsz);

/* Decode one config word into a human-readable "op a,b (imm)" string. */
void cfg_decode(uint32_t word, char *buf, size_t bufsz);

/* Vector A always feeds north and B west, independently of ALU operand slots.
 * Returns whether a live west-edge operand needs B. Compilation validates the
 * remaining semantics; for custom plans this checks direct west-edge reads. */
int mode_requires_b(const dsl_mode *m);

/* Validate every pipeline stage for this geometry, including operand arity and
 * immediate overrides, before issuing any configuration or compute command. */
int pipeline_validate(dsl_ctx *ctx, const dsl_pipeline *pipeline,
                      cgra_info_t geometry, char *err, size_t errsz);

/* Execute a mode on input vectors a[0..n) and optional b[0..n) (b may be
 * NULL for immediate/unary modes). Results go to out[]; returns the number
 * of outputs produced (n for element-wise modes, 1 for reductions) or a
 * negative CGRA_ERR_* on failure. For nonempty diagonal/custom inputs,
 * reset=once (also the default) clears registers at the first chunk of each
 * invocation, then retains state between chunks; reset=each clears every
 * chunk. Reductions clear once even for empty input and return one value.
 * Pattern-inapplicable fields are errors, validated before CFG/reset/run. */
int mode_run(cgra_t *dev, const dsl_mode *m,
             const int16_t *a, const int16_t *b, size_t n,
             long imm_override, int has_imm_override,
             int16_t *out, size_t out_cap,
             char *err, size_t errsz);

/* CLI adapters to the installed cgra_matvec/cgra_scan/cgra_conv API below.
 * New external C callers should include only cgra.h and use those functions.
 * These adapters keep int dimensions and historical return-length conventions.
 * Optional err is written only on failure when errsz > 0. The public API's
 * overlap and partial-output contracts also apply here.
 *
 * Systolic matrix-vector product y = A*x, A row-major M x N, x length N,
 * y length M. Uses the weight-stationary "systolic" dataflow tiled over
 * rows x cols blocks (x stationary in the immediates, matrix streamed row by row);
 * results wrap at 16 bits, like the datapath. Returns 0 or a negative
 * CGRA_ERR_*. */
int matvec_run(cgra_t *dev, const int16_t *A, int M, int N, const int16_t *x,
               int16_t *y, char *err, size_t errsz);

/* Inclusive prefix scan with an associative op (CGRA_OP_ADD/MUL/MAX/MIN):
 * out[i] = a[0] op ... op a[i], computed on row 0 and tiled over columns with
 * the running fold carried between blocks. Returns n or a negative CGRA_ERR_*. */
int scan_run(cgra_t *dev, const int16_t *a, int n, int op, int16_t *out,
             char *err, size_t errsz);

/* Full 1-D convolution out[i] = sum_k h[k]*x[i-k], length n+k-1, realised as a
 * systolic matrix-vector with a Toeplitz matrix. out_cap must hold n+k-1.
 * Returns the output length or a negative CGRA_ERR_*. */
int conv_run(cgra_t *dev, const int16_t *h, int k, const int16_t *x, int n,
             int16_t *out, size_t out_cap, char *err, size_t errsz);

#endif /* CGRA_COMPILE_H */
