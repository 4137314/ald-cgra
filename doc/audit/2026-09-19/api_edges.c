#include "cgra.h"
#include "compile.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
 cgra_t *d=cgra_open("sim:",115200);
 int16_t a[4]={1,2,3,4}, b[4]={10,20,30,40}, y[4]={-1,-1,-1,-1}; char err[256]={0};
 if(argc>1 && !strcmp(argv[1],"overflow")) {int rc=conv_run(d,a,INT_MAX,b,2,y,4,err,sizeof err);cgra_close(d);return rc;}
 if(argc>1 && !strcmp(argv[1],"null")) {int rc=conv_run(d,a,1,b,1,NULL,1,err,sizeof err);cgra_close(d);return rc;}
 int rc=cgra_vec_binop(d,(enum cgra_op)18,a,b,y,4);printf("invalid opcode 18: rc=%d out=%d %d %d %d\n",rc,y[0],y[1],y[2],y[3]);
 rc=cgra_vec_binop(d,CGRA_OP_ADD,a,NULL,y,4);printf("NULL binary b: rc=%d out=%d %d %d %d\n",rc,y[0],y[1],y[2],y[3]);
 rc=scan_run(d,a,4,CGRA_OP_SUB,y,err,sizeof err);printf("scan SUB: rc=%d out=%d %d %d %d\n",rc,y[0],y[1],y[2],y[3]);
 dsl_ctx ctx; dsl_init(&ctx);dsl_load_defaults(&ctx,err,sizeof err);cgra_get_info(d,NULL);cgra_reset_stats(d);
 rc=mode_run(d,dsl_find_mode(&ctx,"dot"),a,b,4,0,0,y,0,err,sizeof err);cgra_stats_t st;cgra_get_stats(d,&st);printf("insufficient output: rc=%d device transactions=%lu\n",rc,st.transactions);
 cgra_close(d);return 0;
}
