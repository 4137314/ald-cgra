/* Reusable plan contracts on independently opened v2/v3 model devices. */
#include "cgra.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(expr, name) do { const int ok = (expr); ++checks; \
    printf("%s plan %d - %s\n", ok ? "ok" : "not ok", checks, name); assert(ok); } while (0)

static void geometry(unsigned rows, unsigned cols, unsigned version)
{
    char backend[40];
    (void)snprintf(backend, sizeof(backend), "sim:%ux%u%s", rows, cols, version == 2 ? ":v2" : "");
    cgra_t *dev = cgra_open(backend, 115200);
    CHECK(dev != NULL, "open plan device");
    uint32_t cfg[CGRA_MAX_PE] = {0};
    cfg[0] = CGRA_CFG(CGRA_OP_ADD, CGRA_SEL_W, CGRA_SEL_N, 0);
    cgra_plan_spec_t spec = {rows, cols, 1, CGRA_EXEC_RESET, 1};
    cgra_plan_t *plan = NULL, *same = NULL;
    CHECK(cgra_plan_create(&spec, cfg, rows*cols, &plan) == CGRA_OK, "compile plan without device I/O");
    CHECK(cgra_plan_create(&spec, cfg, rows*cols, &same) == CGRA_OK, "compile independent equivalent plan");
    cfg[0] = CGRA_CFG(CGRA_OP_CONST, CGRA_SEL_ZERO, CGRA_SEL_ZERO, 99);
    spec.steps = 0; /* original caller buffers may change immediately */
    int16_t west[CGRA_MAX_EDGE] = {32767}, north[CGRA_MAX_EDGE] = {7};
    int16_t taps = 123;
    cgra_stats_t before, after;
    CHECK(cgra_plan_execute_n(dev, plan, west, rows, north, cols, &taps, 1) == CGRA_ERR_STALE,
          "new handle is unbound");
    cgra_get_stats(dev, &before);
    CHECK(before.transactions == 0 && taps == 123, "stale execution has no ID or output side effects");
    CHECK(cgra_plan_bind(dev, plan) == CGRA_OK, "bind explicitly identifies and loads CFG");
    cgra_set_timeout(dev, 100);
    cgra_set_retries(dev, 1);
    cgra_set_transaction_timeout(dev, 500);
    cgra_info_t info;
    CHECK(cgra_get_info(dev, &info) == CGRA_OK, "cached identity keeps binding");
    cgra_get_stats(dev, &before);
    for (int i = 0; i < 8; ++i)
        CHECK(cgra_plan_execute_n(dev, plan, west, rows, north, cols, &taps, 1) == CGRA_OK && taps == -32762,
              "repeated plan execution uses copied CFG/spec and wraps signed addition");
    cgra_get_stats(dev, &after);
    CHECK(after.transactions-before.transactions == (version == 3 ? 8u : 32u), "repeated calls send no extra CFG");
    CHECK(cgra_plan_execute_n(dev, plan, west, rows+1, north, cols, &taps, 1) == CGRA_ERR_ARG,
          "wrong input count rejected");
    CHECK(cgra_plan_execute_n(dev, plan, west, rows, north, cols, &taps, 0) == CGRA_ERR_ARG,
          "wrong tap count rejected");
    CHECK(cgra_plan_execute_n(dev, plan, west, rows, north, cols, west, 1) == CGRA_OK && west[0] == -32762,
          "argument rejection retains binding; output can alias inputs");
    west[0] = 32767;
    cgra_plan_free(plan); plan = NULL;
    CHECK(cgra_plan_execute_n(dev, same, NULL, 0, NULL, 0, &taps, 1) == CGRA_OK && taps == 0,
          "handle owns configuration; equivalent plan survives original free and accepts zero inputs");
    for (int command = 0; command < 7; ++command) {
        CHECK(cgra_plan_bind(dev, same) == CGRA_OK, "explicitly rebind before raw command");
        int rc;
        int16_t regs[CGRA_MAX_PE];
        switch (command) {
        case 0: rc = cgra_identify(dev, NULL); break;
        case 1: rc = cgra_configure_n(dev, cfg, rows*cols); break;
        case 2: rc = cgra_write_inputs_n(dev, west, rows, north, cols); break;
        case 3: rc = cgra_run(dev, 0); break;
        case 4: rc = cgra_read_regs_n(dev, regs, CGRA_MAX_PE); break;
        case 5: rc = cgra_reset_datapath(dev); break;
        default:
            rc = version == 3 ? cgra_exec_n(dev, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0)
                              : cgra_vec_add(dev, west, north, regs, 1);
            break;
        }
        CHECK(rc == CGRA_OK, "raw operation succeeds");
        cgra_get_stats(dev, &before); taps = 123;
        CHECK(cgra_plan_execute_n(dev, same, west, rows, north, cols, &taps, 1) == CGRA_ERR_STALE,
              "every raw command invalidates resident binding");
        cgra_get_stats(dev, &after);
        CHECK(before.transactions == after.transactions && taps == 123, "invalidated execution neither reloads nor changes output");
    }
    CHECK(cgra_plan_bind(dev, same) == CGRA_OK, "bind before explicit invalidation");
    cgra_plan_invalidate(dev);
    CHECK(cgra_plan_execute_n(dev, same, NULL, 0, NULL, 0, &taps, 1) == CGRA_ERR_STALE,
          "out-of-band invalidation is explicit");
    cgra_close(dev);
    dev = cgra_open(backend, 115200);
    CHECK(dev != NULL && cgra_plan_execute_n(dev, same, NULL, 0, NULL, 0, &taps, 1) == CGRA_ERR_STALE,
          "plan outlives handle but reconnection never inherits residency");
    cgra_close(dev);
    cgra_plan_free(same);
}

static void stateful(unsigned version)
{
    cgra_t *dev = cgra_open(version == 3 ? "sim:1x1" : "sim:1x1:v2", 0);
    CHECK(dev != NULL, "open stateful plan device");
    uint32_t cfg = CGRA_CFG(CGRA_OP_MAC, CGRA_SEL_W, CGRA_SEL_N, 0);
    cgra_plan_spec_t spec = {1,1,1,0,1};
    cgra_plan_t *plan = NULL;
    CHECK(cgra_plan_create(&spec, &cfg, 1, &plan) == CGRA_OK && cgra_plan_bind(dev, plan) == CGRA_OK,
          "bind accumulator plan without reset");
    int16_t w=2, n=3, out=0;
    CHECK(cgra_plan_execute_n(dev, plan, &w,1,&n,1,&out,1) == CGRA_OK && out==6, "first MAC");
    CHECK(cgra_plan_execute_n(dev, plan, &w,1,&n,1,&out,1) == CGRA_OK && out==12, "second MAC retains PE state");
    cgra_plan_free(plan); spec.flags=CGRA_EXEC_RESET; spec.steps=255;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan) == CGRA_OK, "compile different transfer for same resident CFG");
    CHECK(cgra_plan_execute_n(dev,plan,&w,1,&n,1,&out,1) == CGRA_OK && out==1530, "reset plus 255 exact captures");
    cgra_plan_free(plan); spec.steps=0;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan) == CGRA_OK &&
          cgra_plan_execute_n(dev,plan,&w,1,&n,1,&out,1) == CGRA_OK && out==0, "reset with zero captures");
    cgra_plan_free(plan); spec.tap_mask=0;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan) == CGRA_OK &&
          cgra_plan_execute_n(dev,plan,NULL,0,NULL,0,NULL,0) == CGRA_OK, "zero tap transfer");
    cgra_plan_free(plan); cgra_close(dev);
}

static void rejected_transfer(unsigned version)
{
    cgra_t *dev=cgra_open(version==3 ? "sim:1x1:flaky" : "sim:1x1:flaky:v2",0);
    CHECK(dev != NULL,"open NACK plan device");
    uint32_t cfg=CGRA_CFG(CGRA_OP_CONST,CGRA_SEL_ZERO,CGRA_SEL_ZERO,17);
    cgra_plan_spec_t spec={1,1,1,CGRA_EXEC_RESET,1}; cgra_plan_t *plan=NULL;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan)==CGRA_OK,"compile NACK plan");
    cgra_set_retries(dev,0);
    CHECK(cgra_plan_bind(dev,plan)==CGRA_ERR_NACK,"failed binding retains original wire error");
    int16_t out=123;
    CHECK(cgra_plan_execute_n(dev,plan,NULL,0,NULL,0,&out,1)==CGRA_ERR_STALE && out==123,
          "failed CFG never establishes residency even if partial writes occurred");
    CHECK(cgra_plan_bind(dev,plan)==CGRA_OK,"explicit rebind after complete NACK");
    CHECK(cgra_plan_execute_n(dev,plan,NULL,0,NULL,0,&out,1)==CGRA_ERR_NACK && out==123,
          "failed planned input transaction preserves output and original error");
    CHECK(cgra_plan_execute_n(dev,plan,NULL,0,NULL,0,&out,1)==CGRA_ERR_STALE,
          "NACK invalidates plan; next call does not silently retry or reload");
    CHECK(cgra_plan_bind(dev,plan)==CGRA_OK &&
          cgra_plan_execute_n(dev,plan,NULL,0,NULL,0,&out,1)==CGRA_OK && out==17,
          "explicit rebind resumes only a session with known frame boundaries");
    cgra_plan_free(plan); cgra_close(dev);
}

static void savings(void)
{
    cgra_t *dev=cgra_open("sim",0); CHECK(dev != NULL,"open plan measurement device");
    CHECK(cgra_get_info(dev,NULL)==CGRA_OK,"exclude initial ID from both measurements");
    uint32_t cfg[16]={CGRA_CFG(CGRA_OP_ADD,CGRA_SEL_W,CGRA_SEL_N,0)};
    cgra_plan_spec_t spec={4,4,1,CGRA_EXEC_RESET,1}; cgra_plan_t *plan=NULL;
    CHECK(cgra_plan_create(&spec,cfg,16,&plan)==CGRA_OK,"compile measurement mapping");
    int16_t west[4]={2},north[4]={3},out=0;
    cgra_reset_stats(dev);
    for (int i=0;i<8;++i)
        CHECK(cgra_configure_n(dev,cfg,16)==CGRA_OK &&
              cgra_exec_n(dev,1,CGRA_EXEC_RESET,1,west,4,north,4,&out,1)==CGRA_OK && out==5,
              "legacy repeated configuration reference");
    cgra_stats_t legacy,resident; cgra_get_stats(dev,&legacy); cgra_reset_stats(dev);
    CHECK(cgra_plan_bind(dev,plan)==CGRA_OK,"include single binding in planned measurement");
    for (int i=0;i<8;++i)
        CHECK(cgra_plan_execute_n(dev,plan,west,4,north,4,&out,1)==CGRA_OK && out==5,
              "planned measurement checks each result");
    cgra_get_stats(dev,&resident);
    CHECK(legacy.transactions==16 && resident.transactions==9 &&
          legacy.tx_bytes+legacy.rx_bytes==744 && resident.tx_bytes+resident.rx_bytes==275,
          "identical mapping/input/taps: 744 to 275 bytes and 16 to 9 transactions including binding");
    cfg[0]=CGRA_CFG(CGRA_OP_CONST,CGRA_SEL_ZERO,CGRA_SEL_ZERO,9);
    cgra_plan_t *different=NULL;
    CHECK(cgra_plan_create(&spec,cfg,16,&different)==CGRA_OK &&
          cgra_plan_execute_n(dev,different,west,4,north,4,&out,1)==CGRA_ERR_STALE,
          "same size but different configuration cannot reuse binding");
    cgra_plan_free(different); cgra_plan_free(plan); cgra_close(dev);
}

int main(void)
{
    const unsigned shapes[][2]={{1,1},{1,16},{2,8},{4,4},{8,2},{16,1},{2,3},{3,2}};
    for (unsigned version=2; version<=3; ++version) {
        for (size_t i=0;i<sizeof(shapes)/sizeof(shapes[0]);++i)
            geometry(shapes[i][0],shapes[i][1],version);
        stateful(version);
        rejected_transfer(version);
    }
    cgra_plan_free(NULL); cgra_plan_invalidate(NULL);
    uint32_t cfg=0; cgra_plan_spec_t spec={1,1,0,0,2}; cgra_plan_t *plan=NULL;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan)==CGRA_ERR_ARG && !plan, "reject absent tap");
    spec.tap_mask=1; spec.rows=17;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan)==CGRA_ERR_ARG && !plan, "reject unsupported plan geometry");
    spec.rows=1; spec.flags=2;
    CHECK(cgra_plan_create(&spec,&cfg,1,&plan)==CGRA_ERR_ARG && !plan, "reject unknown flags");
    CHECK(cgra_plan_create(NULL,&cfg,1,&plan)==CGRA_ERR_ARG && !plan, "reject missing spec");
    savings();
    printf("# %d plan checks passed\n",checks);
    return 0;
}
