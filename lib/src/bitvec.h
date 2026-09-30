/* Internal, C11-defined conversion of a 16-bit bit pattern to its signed value.
 * Converting a negative integer to uint16_t is modulo 2^16; converting the
 * upper half directly back to int16_t would be implementation-defined. */
#ifndef CGRA_BITVEC_H
#define CGRA_BITVEC_H
#include <stdint.h>
static inline int16_t cgra_i16_from_bits(uint16_t bits)
{
    return bits <= INT16_MAX ? (int16_t)bits : (int16_t)((int32_t)bits - 65536);
}
#endif
