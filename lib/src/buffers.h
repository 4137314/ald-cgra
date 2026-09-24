/* Internal checks for borrowed int16_t arrays on supported POSIX hosts. */
#ifndef CGRA_BUFFERS_H
#define CGRA_BUFFERS_H

#include <stddef.h>
#include <stdint.h>

#define CGRA_MAX_ELEMENTS ((size_t)PTRDIFF_MAX / sizeof(int16_t))

static inline int cgra_buffer_valid(const int16_t *p, size_t n)
{
    return n <= CGRA_MAX_ELEMENTS && (n == 0 || p != NULL);
}

/* Counts must already be validated. Integer differences avoid ordering
 * unrelated C pointers or forming an address beyond either object. */
static inline int cgra_buffers_overlap(const int16_t *a, size_t na,
                                      const int16_t *b, size_t nb)
{
    if (!na || !nb) return 0;
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < na * sizeof(*a) : x - y < nb * sizeof(*b);
}

#endif
