/* Clockless functional model; no watchdog or BRAM-inference claim. */
#ifndef CGRA_BLOCK_MODEL_H
#define CGRA_BLOCK_MODEL_H
#include "block_wire.h"

typedef struct {
    uint16_t operands[2][BLOCK_WORDS];
    size_t loaded[2];
    uint16_t result;
    uint32_t executions, session;
    int active, result_valid;
    block_frame last_request, last_response;
} block_model;

/* The caller must supply a frame that passed block_decode. RESET invalidates
 * the session by clearing the whole model; no memory contents need survive. */
void block_model_request(block_model *model, const block_frame *request, block_frame *reply);
#endif
