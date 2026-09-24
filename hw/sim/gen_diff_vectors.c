/* Deterministic array stimuli and expected results from the C emulator. */
#include "cgra.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t state = UINT32_C(0xC6A4A793);

static uint32_t next_random(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static int16_t sample(void)
{
    static const int16_t edge[] = { 0, 1, -1, 32767, -32768, 255, -256 };
    uint32_t v = next_random();
    if ((v & 3u) == 0u)
        return edge[(v >> 2) % (sizeof(edge) / sizeof(edge[0]))];
    return (int16_t)(uint16_t)(v >> 16);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s output-file\n", argv[0]);
        return 2;
    }
    FILE *out = fopen(argv[1], "w");
    cgra_t *dev = cgra_open("sim:", 115200);
    if (out == NULL || dev == NULL) {
        perror("differential vector setup");
        if (out != NULL) fclose(out);
        cgra_close(dev);
        return 1;
    }

    for (unsigned case_no = 0; case_no < 64; ++case_no) {
        unsigned steps = 1u + case_no % 4u;
        uint32_t cfg[CGRA_NUM_PE];
        int16_t west[CGRA_ROWS], north[CGRA_COLS], regs[CGRA_NUM_PE];
        fprintf(out, "%u ", steps);
        for (unsigned i = 0; i < CGRA_NUM_PE; ++i) {
            unsigned op = (case_no + i) % 16u;
            /* Every opcode is paired with every A/B selector combination.
             * Keep selectors independent of op; correlated sequences can
             * appear diverse while never exercising whole mux branches. */
            unsigned sa = case_no % 8u;
            unsigned sb = case_no / 8u;
            cfg[i] = CGRA_CFG(op, sa, sb, sample());
            fprintf(out, "%u ", cfg[i]);
        }
        for (unsigned i = 0; i < CGRA_ROWS; ++i) {
            west[i] = sample();
            fprintf(out, "%d ", west[i]);
        }
        for (unsigned i = 0; i < CGRA_COLS; ++i) {
            north[i] = sample();
            fprintf(out, "%d ", north[i]);
        }
        if (cgra_reset_datapath(dev) != CGRA_OK ||
            cgra_configure(dev, cfg) != CGRA_OK ||
            cgra_write_inputs(dev, west, north) != CGRA_OK ||
            cgra_run(dev, (uint8_t)steps) != CGRA_OK ||
            cgra_read_regs(dev, regs) != CGRA_OK) {
            fprintf(stderr, "emulator failed at case %u\n", case_no);
            fclose(out);
            cgra_close(dev);
            return 1;
        }
        for (unsigned i = 0; i < CGRA_NUM_PE; ++i)
            fprintf(out, "%d%c", regs[i], i + 1 == CGRA_NUM_PE ? '\n' : ' ');
    }
    int bad = ferror(out);
    if (fclose(out) != 0 || bad) {
        fprintf(stderr, "writing differential vectors failed\n");
        cgra_close(dev);
        return 1;
    }
    cgra_close(dev);
    return 0;
}
