#include "block_model.h"
#include <string.h>

static int identical(const block_frame *a, const block_frame *b)
{
    return a->opcode == b->opcode && a->session == b->session &&
           a->sequence == b->sequence && a->length == b->length &&
           memcmp(a->payload, b->payload, a->length) == 0;
}

void block_model_request(block_model *m, const block_frame *q, block_frame *r)
{
    *r = (block_frame){.opcode = (uint8_t)(q->opcode | 0x80u), .session = q->session,
                       .sequence = q->sequence, .length = 1};
    if (q->length > BLOCK_PAYLOAD) { r->payload[0] = BLOCK_ARGUMENT; return; }
    if (m->active && q->session == m->session && q->sequence == m->last_request.sequence) {
        if (identical(q, &m->last_request)) *r = m->last_response;
        else r->payload[0] = BLOCK_CONFLICT;
        return;
    }
    if (q->opcode == BLOCK_HELLO) {
        if (q->length || q->sequence || !q->session) {
            r->payload[0] = BLOCK_ARGUMENT; return;
        }
        if (m->active && q->session == m->session) {
            r->payload[0] = BLOCK_SESSION; return;
        }
        m->active = 1; m->session = q->session;
        m->loaded[0] = m->loaded[1] = 0;
        m->result_valid = 0; m->executions = 0;
        r->length = 6;
        r->payload[0] = BLOCK_OK;
        block_put16(r->payload + 1, BLOCK_WORDS);
        r->payload[3] = BLOCK_CHUNK;
        block_put16(r->payload + 4, 1); /* capability bit 0: resident dot */
    } else {
        if (!m->active || q->session != m->session) {
            r->payload[0] = BLOCK_SESSION; return;
        }
        if (m->last_request.sequence == UINT16_MAX ||
            q->sequence != (uint16_t)(m->last_request.sequence + 1u)) {
            r->payload[0] = BLOCK_SEQUENCE; return;
        }
        r->payload[0] = BLOCK_OK;
        switch (q->opcode) {
        case BLOCK_UPLOAD: {
            if (q->length < 4) { r->payload[0] = BLOCK_ARGUMENT; break; }
            unsigned bank = q->payload[0];
            size_t offset = block_get16(q->payload + 1), count = q->payload[3];
            if (q->length < 4 || bank > 1 || count == 0 || count > BLOCK_CHUNK ||
                q->length != 4 + 2 * count || offset > BLOCK_WORDS || count > BLOCK_WORDS - offset ||
                (offset && offset != m->loaded[bank])) {
                r->payload[0] = BLOCK_ARGUMENT; break;
            }
            for (size_t i = 0; i < count; ++i)
                m->operands[bank][offset + i] = block_get16(q->payload + 4 + 2 * i);
            m->loaded[bank] = offset + count;
            m->result_valid = 0;
            break;
        }
        case BLOCK_DOT: {
            if (q->length != 2) { r->payload[0] = BLOCK_ARGUMENT; break; }
            size_t count = block_get16(q->payload);
            if (q->length != 2 || count > m->loaded[0] || count > m->loaded[1]) {
                r->payload[0] = BLOCK_ARGUMENT; break;
            }
            uint16_t sum = 0;
            for (size_t i = 0; i < count; ++i)
                sum = (uint16_t)((uint32_t)sum + (uint32_t)m->operands[0][i] * m->operands[1][i]);
            m->result = sum; m->result_valid = 1; m->executions++;
            break;
        }
        case BLOCK_RESULT:
            if (q->length) r->payload[0] = BLOCK_ARGUMENT;
            else if (!m->result_valid) r->payload[0] = BLOCK_NOT_READY;
            else {
                r->length = 7;
                block_put16(r->payload + 1, m->result);
                block_put32(r->payload + 3, m->executions);
            }
            break;
        default: r->payload[0] = BLOCK_OPCODE; break;
        }
    }
    /* Valid in-sequence semantic errors are cached too. Bad session/sequence
     * and conflicting duplicates returned above cannot evict this cache. */
    m->last_request = *q;
    m->last_response = *r;
}
