#include "block_wire.h"
#include <string.h>

uint16_t block_get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
uint32_t block_get32(const uint8_t *p)
{
    return (uint32_t)block_get16(p) | ((uint32_t)block_get16(p + 2) << 16);
}
void block_put16(uint8_t *p, uint16_t word)
{
    p[0] = (uint8_t)word; p[1] = (uint8_t)(word >> 8);
}
void block_put32(uint8_t *p, uint32_t word)
{
    block_put16(p, (uint16_t)word); block_put16(p + 2, (uint16_t)(word >> 16));
}

uint16_t block_crc16(const uint8_t *bytes, size_t n)
{
    uint32_t crc = UINT32_C(0xffff);
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint32_t)bytes[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = ((crc << 1) ^ ((crc & UINT32_C(0x8000)) ? UINT32_C(0x1021) : 0)) & UINT32_C(0xffff);
    }
    return (uint16_t)crc;
}

size_t block_encode(const block_frame *frame, uint8_t *wire, size_t capacity)
{
    if (!frame || !wire || frame->length > BLOCK_PAYLOAD) return 0;
    uint8_t raw[BLOCK_RAW], encoded[BLOCK_WIRE];
    raw[0] = BLOCK_VERSION; raw[1] = frame->opcode;
    block_put32(raw + 2, frame->session);
    block_put16(raw + 6, frame->sequence);
    block_put16(raw + 8, (uint16_t)frame->length);
    memcpy(raw + BLOCK_HEADER, frame->payload, frame->length);
    size_t body = BLOCK_HEADER + frame->length;
    block_put16(raw + body, block_crc16(raw, body));
    size_t used = 0;
    encoded[used++] = BLOCK_FLAG;
    for (size_t i = 0; i < body + 2; ++i) {
        if (raw[i] == BLOCK_FLAG || raw[i] == BLOCK_ESCAPE) {
            encoded[used++] = BLOCK_ESCAPE;
            encoded[used++] = raw[i] ^ 0x20u;
        } else encoded[used++] = raw[i];
    }
    encoded[used++] = BLOCK_FLAG;
    if (capacity < used) return 0;
    memcpy(wire, encoded, used);
    return used;
}

int block_decode(block_decoder *decoder, uint8_t byte, block_frame *frame)
{
    if (!decoder || !frame) return -1;
    if (byte == BLOCK_FLAG) {
        if (!decoder->active) {
            decoder->active = 1; decoder->used = 0;
            decoder->escaped = decoder->bad = 0;
            return 0;
        }
        if (!decoder->used && !decoder->escaped && !decoder->bad) return 0;
        size_t n = decoder->used;
        int valid = !decoder->bad && !decoder->escaped && n >= BLOCK_HEADER + 2;
        if (valid) {
            size_t length = block_get16(decoder->raw + 8);
            valid = decoder->raw[0] == BLOCK_VERSION && length <= BLOCK_PAYLOAD &&
                    n == BLOCK_HEADER + length + 2 &&
                    block_get16(decoder->raw + n - 2) == block_crc16(decoder->raw, n - 2);
            if (valid) {
                frame->opcode = decoder->raw[1];
                frame->session = block_get32(decoder->raw + 2);
                frame->sequence = block_get16(decoder->raw + 6);
                frame->length = length;
                memcpy(frame->payload, decoder->raw + BLOCK_HEADER, length);
            }
        }
        decoder->active = decoder->escaped = decoder->bad = 0;
        decoder->used = 0;
        return valid ? 1 : -1;
    }
    if (!decoder->active || decoder->bad) return 0;
    if (decoder->escaped) {
        if (byte != (BLOCK_FLAG ^ 0x20u) && byte != (BLOCK_ESCAPE ^ 0x20u)) {
            decoder->bad = 1; return 0;
        }
        byte ^= 0x20u; decoder->escaped = 0;
    } else if (byte == BLOCK_ESCAPE) {
        decoder->escaped = 1; return 0;
    }
    if (decoder->used == BLOCK_RAW) decoder->bad = 1;
    else decoder->raw[decoder->used++] = byte;
    return 0;
}
