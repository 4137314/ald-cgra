/* Exact codec/model transcripts for production RTL, with a separate signed
 * mathematical oracle for every newly executed DOT. No clock/watchdog model. */
#include "block_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static block_model model;
static block_decoder decoder;
static unsigned transactions;
static void emit(const uint8_t *q, size_t n, const uint8_t *r, size_t nr,
                 unsigned steps, unsigned resets)
{
    fprintf(out, "%zu %zu %u %u", n, nr, steps, resets);
    for (size_t i = 0; i < n; ++i) fprintf(out, " %u", q[i]);
    if (n) for (size_t i = 0; i < nr; ++i) fprintf(out, " %u", r[i]);
    fputc('\n', out); transactions++;
}
static void wire(const uint8_t *bytes, size_t n)
{
    uint8_t reply[BLOCK_WIRE]; size_t nr = 0; unsigned steps = 0, resets = 0;
    block_frame q, r;
    for (size_t i = 0; i < n; ++i) {
        if (block_decode(&decoder, bytes[i], &q) != 1) continue;
        uint32_t before = model.executions;
        block_model_request(&model, &q, &r);
        if (q.opcode == BLOCK_DOT && r.payload[0] == BLOCK_OK && model.executions != before) {
            size_t count = block_get16(q.payload);
            int64_t sum = 0;
            for (size_t j = 0; j < count; ++j) {
                int32_t a = model.operands[0][j], b = model.operands[1][j];
                if (a >= 32768) a -= 65536;
                if (b >= 32768) b -= 65536;
                sum += (int64_t)a*b;
            }
            assert(model.result == (uint16_t)((uint64_t)sum & 65535u));
            steps += (unsigned)count; resets++;
        }
        assert(!nr); /* one outstanding transaction */
        nr = block_encode(&r, reply, sizeof reply); assert(nr);
    }
    emit(bytes, n, reply, nr, steps, resets);
}
static void request(block_frame *q)
{
    uint8_t bytes[BLOCK_WIRE]; size_t n = block_encode(q, bytes, sizeof bytes);
    assert(n); wire(bytes, n);
}
static void reset(void)
{
    emit(NULL, 0, NULL, 1, 0, 0); /* special event: external reset */
    memset(&model, 0, sizeof model); memset(&decoder, 0, sizeof decoder);
    uint8_t q[] = {8}, r[] = {0xcb,1,64,0,4}; emit(q, 1, r, 5, 0, 0);
}
static void timeout(void)
{
    emit(NULL, 0, NULL, 2, 0, 0); /* special event: no bytes for watchdog period */
    memset(&decoder, 0, sizeof decoder);
}
int main(int argc, char **argv)
{
    assert(argc == 2); out = fopen(argv[1], "w"); assert(out);
    reset();
    block_frame q = {.session = 0x7e7d1234};
    request(&q); request(&q); /* HELLO lost response/replay */
    q.sequence = 1; q.opcode = BLOCK_RESULT; request(&q); /* not ready */
    for (unsigned n = 1; n <= BLOCK_CHUNK; ++n) {
        for (unsigned bank = 0; bank < 2; ++bank) {
            q.sequence++; q.opcode = BLOCK_UPLOAD; q.length = 4+2*n;
            q.payload[0] = (uint8_t)bank; block_put16(q.payload+1, 0); q.payload[3] = (uint8_t)n;
            for (unsigned i = 0; i < n; ++i)
                block_put16(q.payload+4+2*i, (uint16_t)(i%3 == 0 ? 0x8000 : i%3 == 1 ? 0x7fff : 0xffff));
            request(&q);
        }
        q.sequence++; q.opcode = BLOCK_DOT; q.length = 2; block_put16(q.payload, (uint16_t)n);
        request(&q); request(&q); /* must not reset/step twice */
        q.payload[0] ^= 1; request(&q); q.payload[0] ^= 1; request(&q); /* conflict retains cache */
        q.sequence++; q.opcode = BLOCK_RESULT; q.length = 0; request(&q);
    }
    /* Full resident capacity in 64-word appends. */
    for (unsigned bank = 0; bank < 2; ++bank) {
        for (unsigned offset = 0; offset < BLOCK_WORDS; offset += BLOCK_CHUNK) {
            q.sequence++; q.opcode = BLOCK_UPLOAD; q.length = BLOCK_PAYLOAD;
            q.payload[0] = (uint8_t)bank; block_put16(q.payload+1, (uint16_t)offset); q.payload[3] = BLOCK_CHUNK;
            for (unsigned i = 0; i < BLOCK_CHUNK; ++i)
                block_put16(q.payload+4+2*i, (uint16_t)((offset+i)*(bank+1)+0x7d00));
            request(&q);
        }
    }
    for (unsigned count = 0; count <= BLOCK_WORDS; count += BLOCK_WORDS/4) {
        q.sequence++; q.opcode = BLOCK_DOT; q.length = 2; block_put16(q.payload, (uint16_t)count); request(&q);
        q.sequence++; q.opcode = BLOCK_RESULT; q.length = 0; request(&q);
    }
    /* Corrupt/incomplete frames must not touch memory, sequence, or PE state.
     * Follow each damaged byte stream with an explicit timeout; the RTL must
     * remain in framed mode, ignoring legacy-looking noise. */
    q.sequence++; q.opcode = BLOCK_DOT; q.length = 2; block_put16(q.payload, 5);
    uint8_t clean[BLOCK_WIRE], damaged[BLOCK_WIRE+1]; size_t len = block_encode(&q, clean, sizeof clean);
    block_model saved = model;
    for (size_t bit = 0; bit < len*8; ++bit) {
        memcpy(damaged, clean, len); damaged[bit/8] ^= (uint8_t)(1u << (bit%8));
        wire(damaged, len); timeout(); assert(!memcmp(&saved, &model, sizeof model));
    }
    for (size_t i = 0; i < len; ++i) {
        memcpy(damaged, clean, i); memcpy(damaged+i, clean+i+1, len-i-1);
        wire(damaged, len-1); timeout(); assert(!memcmp(&saved, &model, sizeof model));
        if (i == 0) continue; /* noise BEFORE the leading flag leaves a valid frame */
        memcpy(damaged, clean, i); damaged[i] = 6; memcpy(damaged+i+1, clean+i, len-i);
        wire(damaged, len+1); timeout(); assert(!memcmp(&saved, &model, sizeof model));
    }
    wire(clean, len-1); timeout(); assert(!memcmp(&saved, &model, sizeof model));
    uint8_t noise[] = {1,2,3,4,5,6,7,8}; wire(noise, sizeof noise);
    request(&q); q.sequence++; q.opcode = BLOCK_RESULT; q.length = 0; request(&q);
    /* Valid CRC but invalid arguments; semantic errors consume/cache seq. */
    for (unsigned n = 0; n < 8; ++n) {
        q.sequence++; q.opcode = BLOCK_UPLOAD; q.length = n; memset(q.payload, 0xff, n); request(&q); request(&q);
    }
    q.sequence++; q.opcode = 0xff; q.length = 0; request(&q);
    q.sequence += 2; q.opcode = BLOCK_RESULT; request(&q); q.sequence -= 1; request(&q);
    q.session ^= 1; q.sequence++; request(&q); q.session ^= 1; request(&q);
    /* Reset in a partial input invalidates the session and parser. */
    wire(clean, len/2); reset(); request(&q);
    q = (block_frame){.session = 0x12345678}; request(&q);
    q.sequence = 1; q.opcode = BLOCK_DOT; q.length = 2; block_put16(q.payload, 0); request(&q);
    q.sequence++; q.opcode = BLOCK_RESULT; q.length = 0; request(&q);
    fputs("0 0 0 0\n", out); assert(!fclose(out));
    printf("block transcripts: %u events; every executed DOT checked by signed int64 oracle\n", transactions);
    return 0;
}
