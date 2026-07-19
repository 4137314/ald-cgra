/*
 * emu.h — in-process emulator of the CGRA device (internal to libcgra).
 *
 * It speaks the exact byte-level UART protocol of hw/rtl/cgra_ctrl.vhd and
 * models the PE mesh of hw/rtl/{pe,cgra_array}.vhd, so the host library and
 * the CLI can be exercised end-to-end without an FPGA:  cgra_open("sim:").
 *
 * Usage: feed host->device bytes with emu_push(), drain device->host bytes
 * with emu_pop().  All command processing is synchronous, so every reply is
 * already queued by the time the caller reads it.
 */

#ifndef CGRA_EMU_H
#define CGRA_EMU_H

#include <stdint.h>

typedef struct cgra_emu cgra_emu_t;

cgra_emu_t *emu_new(void);
void        emu_free(cgra_emu_t *e);

/* Test hook: when enabled, the emulator NACKs the first CFG and the first WR
 * it receives (even with a valid checksum), to exercise host-side retries. */
void        emu_set_flaky(cgra_emu_t *e, int enable);

/* Push one host->device byte; may enqueue reply bytes. */
void emu_push(cgra_emu_t *e, uint8_t byte);

/* Pop one device->host byte. Returns 1 on success, 0 if the queue is empty. */
int  emu_pop(cgra_emu_t *e, uint8_t *byte);

#endif /* CGRA_EMU_H */
