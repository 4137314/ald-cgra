/* Wire comparison through PUBLIC APIs. No emulator-to-FPGA timing inference. */
#include "cgra.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    enum { N=256, REPEAT=8 };
    int16_t a[N], b[N], result; int64_t sum=0;
    for (int i=0;i<N;++i) { a[i]=(int16_t)(i%7-3); b[i]=(int16_t)(i%5-2); sum+=(int64_t)a[i]*b[i]; }
    uint16_t expected=(uint16_t)((uint64_t)sum&65535u);
    char device[64];
    if (snprintf(device,sizeof device,"sim:%s",argv[1]) >= (int)sizeof device) return 2;
    cgra_t *legacy=cgra_open(device,0); cgra_info_t info;
    if (!legacy || cgra_get_info(legacy,&info)) return 1;
    cgra_reset_stats(legacy);
    for (unsigned i=0;i<REPEAT;++i)
        if (cgra_dot(legacy,a,b,N,&result) || (uint16_t)result!=expected) return 1;
    cgra_stats_t ls,bs; cgra_get_stats(legacy,&ls);cgra_close(legacy);
    if (snprintf(device,sizeof device,"sim:blocks:%s",argv[1]) >= (int)sizeof device) return 2;
    cgra_t *blocks=cgra_open(device,0);
    if (!blocks || cgra_block_begin(blocks,0x01020304) ||
        cgra_block_upload(blocks,0,a,N) || cgra_block_upload(blocks,1,b,N)) return 1;
    for (unsigned i=0;i<REPEAT;++i) {
        uint32_t executions;
        if (cgra_block_dot(blocks,N,&result,&executions) || (uint16_t)result!=expected || executions!=i+1) return 1;
    }
    cgra_get_stats(blocks,&bs);cgra_close(blocks);
    printf("{\"geometry\":\"%s\",\"n\":%d,\"repeat\":%d,\"nonce\":16909060,"
           "\"expected_bits\":%u,\"useful_multiplications\":%d,\"useful_additions\":%d,"
           "\"block_captures\":%d,\"legacy\":{\"tx_bytes\":%llu,\"rx_bytes\":%llu,\"transactions\":%llu},"
           "\"blocks\":{\"tx_bytes\":%llu,\"rx_bytes\":%llu,\"transactions\":%llu}}\n",
           argv[1],N,REPEAT,(unsigned)expected,N*REPEAT,(N-1)*REPEAT,N*REPEAT,
           (unsigned long long)ls.tx_bytes,(unsigned long long)ls.rx_bytes,(unsigned long long)ls.transactions,
           (unsigned long long)bs.tx_bytes,(unsigned long long)bs.rx_bytes,(unsigned long long)bs.transactions);
    return 0;
}
