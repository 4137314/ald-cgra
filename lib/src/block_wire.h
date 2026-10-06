/* Experimental block framing v1. Private, shared by transport/model tests. */
#ifndef CGRA_BLOCK_WIRE_H
#define CGRA_BLOCK_WIRE_H
#include <stddef.h>
#include <stdint.h>

#define BLOCK_VERSION 1u
#define BLOCK_WORDS 1024u
#define BLOCK_CHUNK 64u
#define BLOCK_PAYLOAD (4u + 2u * BLOCK_CHUNK)
#define BLOCK_HEADER 10u
#define BLOCK_RAW (BLOCK_HEADER + BLOCK_PAYLOAD + 2u)
#define BLOCK_WIRE (2u * BLOCK_RAW + 2u)
#define BLOCK_FLAG 0x7eu
#define BLOCK_ESCAPE 0x7du

enum block_opcode { BLOCK_HELLO, BLOCK_UPLOAD, BLOCK_DOT, BLOCK_RESULT };
enum block_status { BLOCK_OK, BLOCK_SESSION, BLOCK_SEQUENCE, BLOCK_CONFLICT,
                    BLOCK_OPCODE, BLOCK_ARGUMENT, BLOCK_NOT_READY };

typedef struct {
    uint8_t opcode;
    uint32_t session;
    uint16_t sequence;
    size_t length;
    uint8_t payload[BLOCK_PAYLOAD];
} block_frame;

typedef struct {
    uint8_t raw[BLOCK_RAW];
    size_t used;
    int active, escaped, bad;
} block_decoder;

uint16_t block_crc16(const uint8_t *bytes, size_t n);
/* Zero on invalid arguments/capacity, leaving output untouched. */
size_t block_encode(const block_frame *frame, uint8_t *wire, size_t capacity);
/* 1 complete validated frame, 0 incomplete/idle, -1 discarded malformed frame.
 * Only a return of 1 changes *frame. No effects before CRC/length validation.
 * Reset/timeout discards partial input by clearing the decoder to zero. */
int block_decode(block_decoder *decoder, uint8_t byte, block_frame *frame);
uint16_t block_get16(const uint8_t *bytes);
uint32_t block_get32(const uint8_t *bytes);
void block_put16(uint8_t *bytes, uint16_t word);
void block_put32(uint8_t *bytes, uint32_t word);
#endif
