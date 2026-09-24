/* Byte transcripts for cgra_ctrl + array, using the actual C emulator.
 * Feed each request both in bulk and byte by byte; compare those backends too.
 * The file ends in "0 0", so a truncated transcript cannot silently pass. */
#include "cgra.h"
#include "emu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ID = 1, CFG, WR, RUN, RD, RST, EXEC };
static FILE *output;
static cgra_emu_t *bulk, *bytes;
static unsigned transactions;
static unsigned rows = CGRA_ROWS, cols = CGRA_COLS;
static uint32_t seed = UINT32_C(0x7C6A2026);

static uint32_t random_word(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static void fail(const char *message)
{
    fprintf(stderr, "protocol vector %u: %s\n", transactions, message);
    exit(EXIT_FAILURE);
}

static void transaction(const uint8_t *request, size_t n, size_t expected)
{
    uint8_t a[256], b[256];
    emu_feed(bulk, request, n);
    for (size_t i = 0; i < n; ++i)
        emu_push(bytes, request[i]);
    size_t na = emu_drain(bulk, a, sizeof(a));
    size_t nb = emu_drain(bytes, b, sizeof(b));
    if (na != expected || nb != na || memcmp(a, b, na) != 0)
        fail("bulk/byte emulator replies differ or have wrong length");
    fprintf(output, "%zu %zu", n, na);
    for (size_t i = 0; i < n; ++i) fprintf(output, " %u", request[i]);
    for (size_t i = 0; i < na; ++i) fprintf(output, " %u", a[i]);
    fputc('\n', output);
    ++transactions;
}

static void command(uint8_t cmd)
{
    size_t n = cmd == ID ? 5 : cmd == RD ? 2 * rows * cols + 1 : 1;
    transaction(&cmd, 1, n);
}

static void run(uint8_t steps)
{
    uint8_t request[] = { RUN, steps };
    transaction(request, sizeof(request), 1);
}

static void put16(uint8_t *p, uint16_t word)
{
    p[0] = (uint8_t)word;
    p[1] = (uint8_t)(word >> 8);
}

static void finish_checksum(uint8_t *request, size_t n, int corrupt)
{
    uint8_t sum = 0;
    for (size_t i = 1; i + 1 < n; ++i) sum = (uint8_t)(sum + request[i]);
    request[n - 1] = (uint8_t)(sum ^ (corrupt ? 1u : 0u));
}

static void configure(unsigned phase, int corrupt, int split)
{
    uint8_t request[1 + 4 * CGRA_MAX_PE + 1] = { CFG };
    size_t len = 2 + 4 * rows * cols;
    for (unsigned i = 0; i < rows * cols; ++i) {
        unsigned sa = random_word() % 8;
        unsigned sb = random_word() % 8;
        uint32_t imm = random_word();
        uint32_t word = phase < 2
            ? CGRA_CFG(CGRA_OP_CONST, CGRA_SEL_ZERO, CGRA_SEL_ZERO,
                       (phase ? -200 : 100) + (int)i)
            : CGRA_CFG(i, sa, sb, imm);
        for (unsigned j = 0; j < 4; ++j)
            request[1 + 4 * i + j] = (uint8_t)(word >> (8 * j));
    }
    finish_checksum(request, len, corrupt);
    if (split) {
        size_t cut = len > 10 ? 10 : 3;
        transaction(request, cut, 0);
        transaction(request + cut, len - cut, 1);
    } else {
        transaction(request, len, 1);
    }
}

static void write_inputs(int corrupt)
{
    uint8_t request[2 + 4 * CGRA_MAX_EDGE] = { WR };
    size_t len = 2 + 2 * (rows + cols);
    for (unsigned i = 0; i < rows + cols; ++i)
        put16(request + 1 + 2 * i, (uint16_t)random_word());
    finish_checksum(request, len, corrupt);
    transaction(request, len, 1);
}

static void execute(uint8_t steps, uint8_t flags, uint16_t mask, int corrupt)
{
    uint8_t request[6 + 4 * CGRA_MAX_EDGE] = { EXEC, steps, flags };
    size_t len = 6 + 2 * (rows + cols);
    put16(request + 3, mask);
    for (unsigned i = 0; i < rows + cols; ++i)
        put16(request + 5 + 2 * i, (uint16_t)random_word());
    finish_checksum(request, len, corrupt);
    size_t taps = 0;
    for (unsigned i = 0; i < rows * cols; ++i) taps += (mask >> i) & 1u;
    transaction(request, len, 2 * taps + 2);
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: %s output-file [rows cols]\n", argv[0]);
        return 2;
    }
    if (argc == 4) { rows = (unsigned)strtoul(argv[2], NULL, 10); cols = (unsigned)strtoul(argv[3], NULL, 10); }
    output = fopen(argv[1], "w");
    bulk = emu_new_geometry(rows, cols);
    bytes = emu_new_geometry(rows, cols);
    if (output == NULL || bulk == NULL || bytes == NULL) fail("setup failed");
    command(ID);
    command(0xFF);
    command(RD);
    configure(0, 0, 0);
    run(1);
    command(RD);
    /* A NACK reports a bad checksum; the RTL's streamed CFG/WR writes are
     * already visible. Follow each rejection by RUN/RD to expose that state. */
    configure(1, 1, 1);
    run(1);
    command(RD);
    command(RST);
    command(RD);

    static const uint16_t masks[] = { 0, 0xFFFF, 0x5555, 0xAAAA, 0x8421, 0x8001, 0x7FF0 };
    static const uint8_t counts[] = { 0, 1, 2, 7, 255 };
    for (unsigned i = 0; i < 32; ++i) {
        configure(2, 0, i % 3 == 0);
        write_inputs(0);
        run(counts[i % 5]);
        command(RD);
        write_inputs(1);
        run(1);
        command(RD);
        uint16_t mask = i < 16 ? (uint16_t)(1u << i) : masks[i % 7];
        execute(counts[i % 5], (uint8_t)(i % 2), mask, 0);
        command(RD);
        execute(255, CGRA_EXEC_RESET, mask, 1);
        command(RD);
        run(1); /* EXEC latches inputs even when its checksum is bad. */
        command(RD);
        command(RST);
        command(RD);
    }
    command(ID);
    fputs("0 0\n", output);
    int bad = ferror(output);
    if (fclose(output) != 0 || bad) fail("writing transcript failed");
    emu_free(bulk);
    emu_free(bytes);
    fprintf(stderr, "generated %u protocol transactions (seed 0x7C6A2026)\n", transactions);
    return 0;
}
