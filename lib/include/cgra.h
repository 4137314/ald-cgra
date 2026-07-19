/*
 * cgra.h - host library for the UART-attached CGRA accelerator.
 *
 * The FPGA is a 4x4 mesh of 16-bit processing elements (PEs). The host drives
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

/* Array geometry and protocol version (must match the bitstream). */
#define CGRA_ROWS      4
#define CGRA_COLS      4
#define CGRA_NUM_PE    (CGRA_ROWS * CGRA_COLS)
#define CGRA_DATA_W    16                     /* datapath width, bits */
#define CGRA_PROTO_VER 2

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

/* Opaque device handle. */
typedef struct cgra cgra_t;

/* ---- connection --------------------------------------------------------- */

/*
 * Open a device. 'device' is a serial path (e.g. "/dev/ttyUSB1") or "sim:"
 * for the emulator; 'baud' must match the bitstream (usually 115200) and is
 * ignored by the emulator. Returns NULL on error.
 */
cgra_t     *cgra_open(const char *device, unsigned baud);

/* Close a device and free the handle. */
void        cgra_close(cgra_t *dev);

/* Set the per-reply read timeout, milliseconds (default 2000). */
void        cgra_set_timeout(cgra_t *dev, unsigned ms);

/* Human-readable text for a CGRA_* return code. */
const char *cgra_strerror(int err);

/*
 * Number of automatic retransmissions on a NACK, timeout or bad checksum for
 * the CFG, WR and RD transactions (default 3, 0 disables). RUN is never
 * retried because it advances the datapath.
 */
void cgra_set_retries(cgra_t *dev, unsigned retries);

/* Serial-link instrumentation counters. */
typedef struct {
    unsigned long transactions;   /* high-level commands issued  */
    unsigned long retries;        /* retransmissions performed   */
    unsigned long tx_bytes;       /* bytes written to the device */
    unsigned long rx_bytes;       /* bytes read from the device  */
} cgra_stats_t;

/* Copy the current link statistics into *stats. */
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
 * non-NULL it receives version, rows, cols and datapath width.
 */
int cgra_identify(cgra_t *dev, cgra_info_t *info);

/*
 * Serve the in-process emulator on a fresh pseudo-terminal so external serial
 * tools (or another cgra process via -d /dev/pts/N) can talk to a virtual
 * CGRA. If 'on_ready' is non-NULL it is called once with the slave path just
 * before the blocking serve loop starts; 'user' is passed through to it.
 * POSIX only. Returns a negative CGRA_ERR_* on error.
 */
int cgra_emulate_pty(void (*on_ready)(const char *path, void *user), void *user);

/* ---- low-level primitives ----------------------------------------------- */

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

/*
 * One-shot configure + write inputs + run + read back. Any argument except
 * 'dev' and 'steps' may be NULL to skip that phase (cfg = NULL reuses the
 * loaded configuration). Returns CGRA_OK or a negative CGRA_ERR_*.
 */
int cgra_apply(cgra_t *dev, const uint32_t cfg[CGRA_NUM_PE],
               const int16_t west[CGRA_ROWS], const int16_t north[CGRA_COLS],
               uint8_t steps, int16_t regs[CGRA_NUM_PE]);

/* ---- accelerated kernels ------------------------------------------------ *
 * Element-wise kernels use the "diagonal" config: a[] enters from the north
 * edge, b[] from the west, PASS chains route them to the diagonal PEs, which
 * compute up to 4 results per transaction.
 */

/* out[i] = a[i] <op> b[i] for a binary cgra_op. */
int cgra_vec_binop(cgra_t *dev, enum cgra_op op,
                   const int16_t *a, const int16_t *b, int16_t *out, size_t n);

/* out[i] = a[i] <op> imm (operand b tied to the immediate). */
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

#ifdef __cplusplus
}
#endif

#endif /* CGRA_H */
