/*
 * cgra.h - host library for the UART-attached CGRA accelerator.
 *
 * The FPGA is an R x C mesh of 16-bit processing elements (default 4x4,
 * at most 16 PEs in protocol v3). The host drives
 * it over a serial link: load one 32-bit config word per PE, write operands
 * into the west (per-row) and north (per-column) edge registers, step the
 * array a given number of cycles, then read every PE register back.
 *
 * Open a device with cgra_open(). The magic path "sim:" (or "sim:...") is an
 * in-process emulator that speaks the exact same byte protocol, so the whole
 * stack runs with no FPGA attached.
 *
 * SYNC: the protocol bytes and the config-word layout below are mirrored in
 *       hw/rtl/cgra_pkg.vhd (device) and lib/src/emu.c (emulator). Change all
 *       three together or things quietly break.
 *
 * Config word (32 bit, one per PE, little-endian on the wire):
 *
 *   31      26 25   23 22   20 19   16 15            0
 *  +----------+-------+-------+-------+----------------+
 *  | reserved | sel_b | sel_a |  op   |      imm       |
 *  +----------+-------+-------+-------+----------------+
 */

#ifndef CGRA_H
#define CGRA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default geometry for the legacy fixed-size primitives. Runtime geometry is
 * returned by cgra_get_info(). Protocol v3 supports at most 16 16-bit PEs. */
#define CGRA_ROWS      4
#define CGRA_COLS      4
#define CGRA_NUM_PE    (CGRA_ROWS * CGRA_COLS)
#define CGRA_DATA_W    16                     /* datapath width, bits */
#define CGRA_PROTO_VER 3
#define CGRA_MAX_PE    16
#define CGRA_MAX_EDGE  16

/*
 * PE opcodes (config bits [19:16]). Arithmetic is 16-bit signed and wraps;
 * MUL/MAC keep the low 16 bits of the product.
 */
enum cgra_op {
    CGRA_OP_NOP   = 0x0,  /* hold register        */
    CGRA_OP_PASS  = 0x1,  /* r = a                */
    CGRA_OP_ADD   = 0x2,  /* r = a + b            */
    CGRA_OP_SUB   = 0x3,  /* r = a - b            */
    CGRA_OP_MUL   = 0x4,  /* r = (a * b) & 0xFFFF */
    CGRA_OP_MAC   = 0x5,  /* r = r + a * b        */
    CGRA_OP_AND   = 0x6,  /* r = a & b            */
    CGRA_OP_OR    = 0x7,  /* r = a | b            */
    CGRA_OP_XOR   = 0x8,  /* r = a ^ b            */
    CGRA_OP_SHL   = 0x9,  /* r = a << (b & 0xF)   */
    CGRA_OP_SHR   = 0xA,  /* r = a >> (b & 0xF)   (arithmetic) */
    CGRA_OP_MAX   = 0xB,  /* r = max(a, b) signed */
    CGRA_OP_MIN   = 0xC,  /* r = min(a, b) signed */
    CGRA_OP_ABS   = 0xD,  /* r = |a|              */
    CGRA_OP_ACC   = 0xE,  /* r = r + a            */
    CGRA_OP_CONST = 0xF   /* r = imm              */
};

/*
 * Operand mux selects (config bits [22:20] = a, [25:23] = b).
 * At the edges, N on row 0 reads that column's north input port and W on
 * column 0 reads that row's west input port; the S and E edges read zero.
 */
enum cgra_sel {
    CGRA_SEL_N     = 0,   /* north neighbour / north input port     */
    CGRA_SEL_S     = 1,   /* south neighbour (last row: zero)       */
    CGRA_SEL_E     = 2,   /* east neighbour (last column: zero)     */
    CGRA_SEL_W     = 3,   /* west neighbour / west input port       */
    CGRA_SEL_CONST = 4,   /* 16-bit immediate from the config word  */
    CGRA_SEL_SELF  = 5,   /* own register (feedback)                */
    CGRA_SEL_ZERO  = 6    /* constant zero                          */
};

/* Pack one 32-bit PE config word from op, the two selects and a 16-bit imm. */
#define CGRA_CFG(op, sel_a, sel_b, imm)                  \
    (((uint32_t)((uint16_t)(imm)))                       \
     | (((uint32_t)(op)    & 0xFu) << 16)                \
     | (((uint32_t)(sel_a) & 0x7u) << 20)                \
     | (((uint32_t)(sel_b) & 0x7u) << 23))

/* Return codes: CGRA_OK is 0, every error is negative. */
#define CGRA_OK           0   /* success                          */
#define CGRA_ERR_IO      -1   /* syscall / port error             */
#define CGRA_ERR_TIMEOUT -2   /* no reply from the device         */
#define CGRA_ERR_NACK    -3   /* device rejected the command      */
#define CGRA_ERR_PROTO   -4   /* unexpected byte / bad signature  */
#define CGRA_ERR_ARG     -5   /* invalid argument                 */
#define CGRA_ERR_GEOMETRY -6  /* unsupported/mismatched geometry   */
#define CGRA_ERR_DESYNC   -7  /* uncertain parser; session stopped */
#define CGRA_ERR_NOMEM    -8  /* memory allocation failed          */

/* Opaque owned handle. Close exactly once; cgra_close(NULL) is harmless.
 * Calls are synchronous. All caller buffers and device strings are borrowed
 * only for the duration of a call; the library never frees them. Buffers must
 * be valid, aligned arrays of the documented length. No internal locking:
 * serialize ALL calls (including stats/setters/close) on a handle, and all
 * access to one physical device, across threads/processes/handles. Independent
 * devices or emulator handles may be used concurrently. */
typedef struct cgra cgra_t;

/* ---- connection --------------------------------------------------------- */

/*
 * Open a device. 'device' is a serial path (e.g. "/dev/ttyUSB1") or "sim:"
 * for the emulator; 'baud' must match the bitstream (usually 115200) and is
 * ignored by the emulator. "sim:2x3" selects a rectangular array;
 * "sim:2x3:v2" and "sim:2x3:flaky" select its test variants.
 * Rows/columns must be positive and rows*columns <= CGRA_MAX_PE.
 * Returns NULL on error with errno set (EINVAL for invalid device/baud,
 * ENOMEM for allocation failure, otherwise the failing POSIX operation).
 * The device string need not remain alive after this call.
 */
cgra_t     *cgra_open(const char *device, unsigned baud);

/* Close a device and free the handle. After a transport/protocol failure,
 * first restore the controller to idle before reopening: reset the device
 * out of band, or wait for its configured parser watchdog and pending TX/RX
 * to finish. Reopening alone does NOT reset the remote parser. Configuration,
 * inputs and datapath must be reinitialized before computing again. */
void        cgra_close(cgra_t *dev);

/* Set the deadline for one complete reply, milliseconds (default 2000).
 * Partial reads and interrupted waits do not restart it. Each retry has its
 * own reply deadline; this setting does not bound blocking writes. */
void        cgra_set_timeout(cgra_t *dev, unsigned ms);

/* Borrowed immutable static text for a CGRA_* code; never free or modify it.
 * Except for cgra_open(), use return codes, not errno, to diagnose errors. */
const char *cgra_strerror(int err);

/*
 * Retransmissions after a complete NACK for CFG, WR and EXEC with RESET
 * (default 3, 0 disables). Assumes intact command bytes and frame lengths;
 * v2/v3 cannot reliably detect dropped/inserted bytes or corrupted opcodes.
 * Timeout, IO errors and malformed replies stop the session; subsequent
 * protocol calls return CGRA_ERR_DESYNC without sending any bytes. RUN and
 * RD are never retried. The original failing call returns its original error.
 * A failed command can have changed device state; there is no rollback.
 */
void cgra_set_retries(cgra_t *dev, unsigned retries);

/* Serial-link instrumentation counters. */
typedef struct {
    unsigned long transactions;   /* high-level commands issued  */
    unsigned long retries;        /* retransmissions performed   */
    unsigned long tx_bytes;       /* bytes written to the device */
    unsigned long rx_bytes;       /* bytes read from the device  */
} cgra_stats_t;

/* Copy the current link statistics into *stats. NULL dev/stats is a no-op.
 * Setters and cgra_reset_stats also do nothing for a NULL handle. */
void cgra_get_stats(cgra_t *dev, cgra_stats_t *stats);

/* Zero the link statistics. */
void cgra_reset_stats(cgra_t *dev);

/* Device identity returned by the ID handshake. */
typedef struct {
    uint8_t version;   /* protocol version       */
    uint8_t rows;      /* array rows             */
    uint8_t cols;      /* array columns          */
    uint8_t data_w;    /* datapath width, bits   */
} cgra_info_t;

/*
 * ID handshake; verifies the device signature. If 'version' is non-NULL it
 * receives the protocol version. Returns CGRA_OK or a negative CGRA_ERR_*.
 */
int cgra_ping(cgra_t *dev, uint8_t *version);

/*
 * ID handshake returning the full geometry (protocol v2+). If 'info' is
 * non-NULL it receives version, rows, cols and datapath width on success.
 * Always sends ID and refreshes the cache; it does not validate geometry.
 */
int cgra_identify(cgra_t *dev, cgra_info_t *info);

/* Cached identity for transfers/kernels. Validates protocol v2/v3, 16-bit data
 * and positive geometry with at most CGRA_MAX_PE PEs. Performs one ID on the
 * first call. Unsupported geometry returns CGRA_ERR_GEOMETRY, unsupported
 * protocol CGRA_ERR_PROTO. Output is unchanged on failure. Cache is discarded
 * after an uncertain transfer or close; there is no hot-plug detection.
 * After reprogramming/reconnection, restore the remote parser, reopen and
 * initialize the device again. */
int cgra_get_info(cgra_t *dev, cgra_info_t *info);

/*
 * Serve the in-process emulator on a fresh pseudo-terminal so external serial
 * tools (or another cgra process via -d /dev/pts/N) can talk to a virtual
 * CGRA. If 'on_ready' is non-NULL it is called once with the slave path just
 * before the blocking serve loop starts; 'user' is passed through to it.
 * The path is borrowed during the callback; copy it if retaining it.
 * Startup uses ptsname's shared storage: run PTY servers in separate
 * processes, not concurrently with other ptsname users in one process.
 * POSIX only. Returns a negative CGRA_ERR_* on error.
 */
int cgra_emulate_pty(void (*on_ready)(const char *path, void *user), void *user);

/* ---- low-level primitives ----------------------------------------------- */

/* Geometry-aware transfers. cfg count and each input count must equal the
 * device dimensions; read capacity must be at least rows*cols. Counts are in
 * words, not bytes. Buffer errors return CGRA_ERR_ARG before writing a command
 * (the initial ID handshake may still occur). */
int cgra_configure_n(cgra_t *dev, const uint32_t *cfg, size_t ncfg);
int cgra_write_inputs_n(cgra_t *dev, const int16_t *west, size_t nwest,
                        const int16_t *north, size_t nnorth);
int cgra_read_regs_n(cgra_t *dev, int16_t *regs, size_t capacity);

/* The legacy configure/write/read/apply/exec functions require exactly a 4x4
 * device. They return CGRA_ERR_GEOMETRY for other dimensions. Vector and dot
 * kernels below adapt to the actual geometry. */

/* Load a full array configuration, one word per PE, row-major. */
int cgra_configure(cgra_t *dev, const uint32_t cfg[CGRA_NUM_PE]);

/* Write the edge inputs: west[r] feeds row r (col 0), north[c] feeds col c (row 0). */
int cgra_write_inputs(cgra_t *dev, const int16_t west[CGRA_ROWS],
                      const int16_t north[CGRA_COLS]);

/* Clock the whole array for 'steps' cycles (0..255). */
int cgra_run(cgra_t *dev, uint8_t steps);

/* Read back all PE registers, row-major: regs[r * CGRA_COLS + c]. */
int cgra_read_regs(cgra_t *dev, int16_t regs[CGRA_NUM_PE]);

/* Clear all PE registers; the loaded configuration is kept. */
int cgra_reset_datapath(cgra_t *dev);

/* ---- fused execute (protocol v3) ---------------------------------------- *
 * cgra_exec() collapses the write-inputs / run / read-back triple that every
 * kernel issues per chunk into a SINGLE round trip, and reads back only the
 * registers the caller actually taps. On a link where the round trip, not the
 * bit rate, dominates (a USB-serial bridge with a latency timer, say) this is
 * the difference between three turnarounds per chunk and one; on a bit-rate
 * bound link it still roughly halves the bytes, because a 4-lane element-wise
 * chunk no longer ships all 16 registers back.
 */

/* flags for cgra_exec(): clear the PE registers before stepping (a fused
 * cgra_reset_datapath). Allows retry after a complete, checksum-valid NACK,
 * under the cgra_set_retries fault model. Never allows retry after timeout. */
#define CGRA_EXEC_RESET 0x01u

/*
 * Write the edge inputs, optionally clear the datapath, advance the array
 * 'steps' cycles and read back the PE registers selected by 'tap_mask' (bit i
 * selects register i, row-major), all in one transaction.
 *
 * 'taps' receives the selected registers in ascending PE index and must have
 * room for exactly popcount(tap_mask) values ('ntaps'); pass tap_mask = 0 and
 * taps = NULL for a pure fused write+run. 'west'/'north' may be NULL to inject
 * zeros. Returns CGRA_OK or a negative CGRA_ERR_*, including CGRA_ERR_PROTO if
 * the device does not implement protocol v3 (use cgra_has_exec() to check).
 */
int cgra_exec(cgra_t *dev, uint8_t steps, unsigned flags, uint16_t tap_mask,
              const int16_t west[CGRA_ROWS], const int16_t north[CGRA_COLS],
              int16_t *taps, size_t ntaps);

/* Geometry-aware EXEC. Non-NULL west/north need exact row/column counts;
 * NULL with count 0 injects zeros. ntaps must equal popcount(tap_mask).
 * Bits selecting absent PEs and unknown flag bits return CGRA_ERR_ARG.
 * The wire mask remains 16 bits for every supported geometry. */
int cgra_exec_n(cgra_t *dev, uint8_t steps, unsigned flags, uint16_t tap_mask,
                const int16_t *west, size_t nwest,
                const int16_t *north, size_t nnorth,
                int16_t *taps, size_t ntaps);

/*
 * Non-zero if the attached device implements cgra_exec (supported protocol v3).
 * The answer is discovered with one ID handshake and cached, so callers may
 * ask per chunk. Kernels use this to fall back to the v2 write/run/read path
 * on an older bitstream. Zero also covers errors; to distinguish them, call
 * cgra_get_info(), check its return code, then inspect info.version >= 3.
 */
int cgra_has_exec(cgra_t *dev);

/*
 * One-shot configure + write inputs + run + read back. Any argument except
 * 'dev' and 'steps' may be NULL. cfg = NULL reuses the configuration;
 * west = north = NULL retains both input edges. If only one edge is NULL,
 * that edge receives zeros. regs = NULL skips readback; RUN is always done.
 * Returns CGRA_OK or a negative CGRA_ERR_*.
 */
int cgra_apply(cgra_t *dev, const uint32_t cfg[CGRA_NUM_PE],
               const int16_t west[CGRA_ROWS], const int16_t north[CGRA_COLS],
               uint8_t steps, int16_t regs[CGRA_NUM_PE]);

/* ---- accelerated kernels ------------------------------------------------ *
 * Element-wise kernels use the "diagonal" config: a[] enters from the north
 * edge, b[] from the west, PASS chains route them to the diagonal PEs, which
 * compute min(rows, cols) results per transaction.
 *
 * All kernels return CGRA_OK or a negative CGRA_ERR_*, never a result length.
 * Lengths/capacities are element counts, not bytes; array footprints must fit
 * PTRDIFF_MAX bytes. The caller owns storage; claimed capacities cannot prove
 * the actual allocation size. Argument errors are detected before any I/O or
 * output change. Kernels replace device configuration/inputs/registers and do
 * not restore them. On a runtime failure, vector/scan output may contain a
 * completed prefix; matvec/conv output may contain zeroed/partial sums. Dot
 * stores its scalar only on success. Discard partial output on any error.
 *
 * Vector and scan output may equal an input exactly; other overlap with an
 * input is rejected. Matvec/conv output must not overlap any input. Inputs
 * may overlap one another. Dot's scalar may alias either input.
 *
 * Empty vector/scan inputs (n=0) are successful no-ops; pointers may be NULL.
 * Empty dot writes zero to the required result pointer. Matvec/conv empty
 * dimensions are defined below. Empty operations perform no I/O and do not
 * check the device's remote state. A non-NULL handle and valid op are still
 * required. Arithmetic wraps modulo 2^16, MIN/MAX compare signed values,
 * shifts use the low four bits of B and SHR sign-extends. At PE level,
 * ABS(-32768) is -32768.
 */

/* out[i] = a[i] <op> b[i]. Supported stateless binary operations: ADD, SUB,
 * MUL, AND, OR, XOR, SHL, SHR, MAX and MIN. Other values return CGRA_ERR_ARG
 * before device I/O; use the low-level API for stateful PE operations. */
int cgra_vec_binop(cgra_t *dev, enum cgra_op op,
                   const int16_t *a, const int16_t *b, int16_t *out, size_t n);

/* out[i] = a[i] <op> imm; the same supported operations as cgra_vec_binop. */
int cgra_vec_binop_imm(cgra_t *dev, enum cgra_op op,
                       const int16_t *a, int16_t imm, int16_t *out, size_t n);

int cgra_vec_add (cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n); /* a + b     */
int cgra_vec_sub (cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n); /* a - b     */
int cgra_vec_mul (cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n); /* a * b     */
int cgra_vec_min (cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n); /* min(a,b)  */
int cgra_vec_max (cgra_t *dev, const int16_t *a, const int16_t *b, int16_t *out, size_t n); /* max(a,b)  */
int cgra_vec_relu(cgra_t *dev, const int16_t *a, int16_t *out, size_t n);                   /* max(a,0)  */
int cgra_vec_addi(cgra_t *dev, const int16_t *a, int16_t imm, int16_t *out, size_t n);      /* a + imm   */
int cgra_vec_muli(cgra_t *dev, const int16_t *a, int16_t imm, int16_t *out, size_t n);      /* a * imm   */

/* Dot product on the MAC PE (16-bit wrap-around accumulator). */
int cgra_dot(cgra_t *dev, const int16_t *a, const int16_t *b, size_t n,
             int16_t *result);

/* y = A*x. A is row-major rows*cols, x has cols elements, y_capacity >= rows.
 * rows=0 is a no-op with all buffers optional. With rows>0, cols=0, y is
 * required and filled with zeros without I/O; A/x may be NULL.
 * Row 0 computes products, later rows delay them; the host sums partials. */
int cgra_matvec(cgra_t *dev, const int16_t *A, size_t rows, size_t cols,
                const int16_t *x, int16_t *y, size_t y_capacity);

/* Inclusive prefix scan: out[i] = a[0] op ... op a[i]. Supported operations:
 * ADD, MUL, MIN, MAX. out_capacity >= n; out==a is supported. */
int cgra_scan(cgra_t *dev, enum cgra_op op, const int16_t *a, size_t n,
              int16_t *out, size_t out_capacity);

/* Full convolution: out[i] = sum_j h[j]*x[i-j], with zero extension.
 * k/n are lengths of h/x. Nonempty output has n+k-1 elements; out_capacity
 * must hold them. If either input is empty, output is empty and all pointers
 * may be NULL. Uses a dense (n+k-1)*n Toeplitz workspace; CGRA_ERR_NOMEM if
 * allocation fails, CGRA_ERR_ARG if dimensions/footprint cannot fit. */
int cgra_conv(cgra_t *dev, const int16_t *h, size_t k,
              const int16_t *x, size_t n, int16_t *out, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* CGRA_H */
