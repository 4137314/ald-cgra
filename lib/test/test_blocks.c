/* Experimental block codec/model; no claim of RTL or public API integration. */
#include "cgra.h"
#include "../src/block_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr, name) do { int ok = (expr); ++checks; \
    printf("%s block %u - %s\n", ok ? "ok" : "not ok", checks, name); assert(ok); } while (0)

static int same(const block_frame *a, const block_frame *b)
{
    return a->opcode == b->opcode && a->session == b->session && a->sequence == b->sequence &&
           a->length == b->length && !memcmp(a->payload, b->payload, a->length);
}

static int decode(const uint8_t *wire, size_t n, block_frame *out)
{
    block_decoder d = {0};
    int frames = 0;
    for (size_t i = 0; i < n; ++i) if (block_decode(&d, wire[i], out) == 1) ++frames;
    return frames;
}

static void framing(void)
{
    CHECK(block_crc16((const uint8_t *)"123456789", 9) == 0x29b1, "CRC fixed check vector");
    CHECK(block_crc16(NULL, 0) == 0xffff, "CRC empty message starts at FFFF");
    uint8_t wire[BLOCK_WIRE], bad[BLOCK_WIRE + 2];
    block_frame q = {.opcode=BLOCK_UPLOAD, .session=0x7e7d0102, .sequence=0x7d7e};
    for (size_t length = 0; length <= BLOCK_PAYLOAD; ++length) {
        q.length = length;
        for (size_t i = 0; i < length; ++i) q.payload[i] = (uint8_t)(i * 73 + length);
        size_t n = block_encode(&q, wire, sizeof(wire));
        block_frame got = {0};
        CHECK(n >= BLOCK_HEADER + length + 4 && decode(wire,n,&got)==1 && same(&q,&got),
              "roundtrip each permitted payload length including escapes");
        memset(bad,0xa5,sizeof(bad));
        CHECK(block_encode(&q,bad,n-1)==0 && bad[0]==0xa5 && bad[n-2]==0xa5,
              "insufficient capacity leaves output untouched");
    }
    q.length = 12;
    for (size_t i=0; i<q.length; ++i) q.payload[i]=(uint8_t)(i*17);
    size_t n=block_encode(&q,wire,sizeof(wire));
    for (size_t i=0; i<n; ++i) {
        for (unsigned bit=0; bit<8; ++bit) {
            memcpy(bad,wire,n);bad[i]^=(uint8_t)(1u<<bit);
            block_frame out={.opcode=99};
            CHECK(decode(bad,n,&out)==0 && out.opcode==99,
                  "every single wire-bit corruption in the fixture is rejected before output");
        }
        memcpy(bad,wire,i);memcpy(bad+i,wire+i+1,n-i-1);
        block_frame out={0};
        CHECK(decode(bad,n-1,&out)==0,"each lost byte in the fixture prevents a frame");
    }
    for (size_t i=0; i<=n; ++i) {
        memcpy(bad,wire,i);bad[i]=0x55;memcpy(bad+i+1,wire+i,n-i);
        block_frame out={0};int frames=decode(bad,n+1,&out);
        CHECK((i==0 || i==n) ? frames==1 && same(&q,&out) : frames==0,
              "inserted body byte is rejected; noise outside delimiters leaves frame unchanged");
    }
    block_decoder d={0};block_frame out={.opcode=99};
    CHECK(block_decode(&d,BLOCK_FLAG,&out)==0,"start overlong frame");
    for(size_t i=0;i<BLOCK_RAW+3;++i) CHECK(block_decode(&d,0x55,&out)==0,"bounded overlong accumulation");
    CHECK(block_decode(&d,BLOCK_FLAG,&out)==-1 && out.opcode==99,"overlong frame discarded at boundary");
    int complete=0;
    for(size_t i=0;i<n;++i) complete+=block_decode(&d,wire[i],&out)==1;
    CHECK(complete==1 && same(&q,&out),"fresh frame resynchronizes after overrun");
    const uint8_t dangling[]={BLOCK_FLAG,BLOCK_ESCAPE,BLOCK_FLAG};
    CHECK(decode(dangling,sizeof(dangling),&out)==0,"dangling escape cannot publish a frame");
    q.length=BLOCK_PAYLOAD+1;
    CHECK(!block_encode(&q,wire,sizeof(wire)),"oversized payload rejected by encoder");
}

static uint64_t wire_bytes, transactions;
static block_frame exchange(block_model *m, const block_frame *q)
{
    uint8_t wire[BLOCK_WIRE];block_frame decoded={0}, response={0}, returned={0};
    size_t n=block_encode(q,wire,sizeof(wire));
    CHECK(n && decode(wire,n,&decoded)==1,"request passes real framing and CRC");
    wire_bytes+=n;transactions++;
    block_model_request(m,&decoded,&response);
    n=block_encode(&response,wire,sizeof(wire));
    CHECK(n && decode(wire,n,&returned)==1,"response passes real framing and CRC");
    wire_bytes+=n;
    CHECK(returned.opcode==(uint8_t)(q->opcode|0x80u) && returned.session==q->session &&
          returned.sequence==q->sequence,"response echoes protected transaction identity");
    return returned;
}

static void resident(void)
{
    block_model m={0};block_frame q={.opcode=BLOCK_HELLO,.session=0x01020304};
    wire_bytes=transactions=0;
    block_frame r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_OK && block_get16(r.payload+1)==BLOCK_WORDS &&
          r.payload[3]==BLOCK_CHUNK && block_get16(r.payload+4)==1,"HELLO advertises resident dot bounds");
    int16_t a[256],b[256];int64_t scalar=0;
    for(unsigned i=0;i<256;++i) {
        a[i]=(int16_t)((int)(i%7)-3); b[i]=(int16_t)((int)(i%5)-2);
        scalar+=(int64_t)a[i]*b[i];
    }
    for(unsigned bank=0;bank<2;++bank) for(unsigned offset=0;offset<256;offset+=BLOCK_CHUNK) {
        q.opcode=BLOCK_UPLOAD;q.sequence++;q.length=4+2*BLOCK_CHUNK;
        q.payload[0]=(uint8_t)bank;block_put16(q.payload+1,(uint16_t)offset);q.payload[3]=BLOCK_CHUNK;
        for(unsigned i=0;i<BLOCK_CHUNK;++i) block_put16(q.payload+4+2*i,(uint16_t)(bank?b[offset+i]:a[offset+i]));
        r=exchange(&m,&q);CHECK(r.payload[0]==BLOCK_OK,"upload one validated chunk");
    }
    for(unsigned iteration=0;iteration<8;++iteration) {
        q.opcode=BLOCK_DOT;q.sequence++;q.length=2;block_put16(q.payload,256);
        r=exchange(&m,&q);CHECK(r.payload[0]==BLOCK_OK,"execute on resident vectors");
        q.opcode=BLOCK_RESULT;q.sequence++;q.length=0;r=exchange(&m,&q);
        CHECK(r.payload[0]==BLOCK_OK && block_get16(r.payload+1)==(uint16_t)scalar &&
              block_get32(r.payload+3)==iteration+1,"independent signed scalar oracle checks every result");
    }
    uint64_t block_bytes=wire_bytes,block_transactions=transactions;
    cgra_t *dev=cgra_open("sim:4x4",115200);CHECK(dev!=NULL,"open existing legacy backend");
    CHECK(cgra_get_info(dev,NULL)==CGRA_OK,"exclude initial legacy ID from measurement");
    cgra_reset_stats(dev);
    for(unsigned iteration=0;iteration<8;++iteration) {
        int16_t result=0;
        CHECK(cgra_dot(dev,a,b,256,&result)==CGRA_OK && (uint16_t)result==(uint16_t)scalar,
              "same scalar oracle checks every legacy execution");
    }
    cgra_stats_t stats;cgra_get_stats(dev,&stats);cgra_close(dev);
    CHECK(block_bytes < stats.tx_bytes+stats.rx_bytes && block_transactions < stats.transactions,
          "framed resident model reduces measured bytes and transactions on repeated dot256");
    printf("# resident model dot256 repeat8: block bytes=%llu transactions=%llu; legacy bytes=%llu transactions=%llu\n",
           (unsigned long long)block_bytes,(unsigned long long)block_transactions,
           (unsigned long long)(stats.tx_bytes+stats.rx_bytes),(unsigned long long)stats.transactions);
    q.opcode=BLOCK_DOT;q.sequence++;q.length=2;block_put16(q.payload,256);
    r=exchange(&m,&q);block_frame duplicate=exchange(&m,&q);
    CHECK(same(&r,&duplicate) && m.executions==9,"lost response replay returns cached result without duplicate execution");
    q.payload[0]^=1;r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_CONFLICT && m.executions==9,"same ID with changed bytes cannot execute or evict cache");
    q.payload[0]^=1;r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_OK && m.executions==9,"original request remains replayable after conflict");
    q.sequence+=2;r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_SEQUENCE && m.executions==9,"skipped sequence rejected without effects");
    q.sequence--;q.opcode=BLOCK_UPLOAD;q.length=4;q.payload[0]=0;block_put16(q.payload+1,1024);q.payload[3]=1;
    r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_ARGUMENT && m.loaded[0]==256 && m.result_valid,
          "semantic error preserves operands/result but consumes and caches its sequence");
    duplicate=exchange(&m,&q);CHECK(same(&r,&duplicate),"semantic error duplicate is stable");
    memset(&m,0,sizeof(m));r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_SESSION && !m.active,"reset drops dedup/session: old request cannot execute");
    q=(block_frame){.opcode=BLOCK_HELLO,.session=0x02030405};r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_OK && !m.loaded[0] && !m.result_valid,"fresh session invalidates resident data");
    q.opcode=BLOCK_DOT;q.sequence=1;q.length=2;block_put16(q.payload,0);r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_OK && m.result==0 && m.executions==1,"empty resident dot is defined");
    /* Advance every ID through the actual model, no test writes its counter. */
    q.opcode=0x7f;q.length=0;
    for(unsigned id=2;id<=UINT16_MAX;++id) {
        q.sequence=(uint16_t)id;block_model_request(&m,&q,&r);
        assert(r.payload[0]==BLOCK_OPCODE);
    }
    q.sequence=0;block_model_request(&m,&q,&r);
    CHECK(r.payload[0]==BLOCK_SEQUENCE && m.executions==1,"sequence exhaustion refuses wrap in the same session");
    q=(block_frame){.opcode=BLOCK_HELLO,.session=0x03040506};r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_OK,"new nonce explicitly renews an exhausted session");
}

static void boundaries(void)
{
    block_model m={0}; block_frame q={.opcode=BLOCK_HELLO,.session=0x11223344},r;
    block_model_request(&m,&q,&r);
    uint16_t values[2][BLOCK_WORDS];
    int64_t scalar=0;
    for (unsigned i=0;i<BLOCK_WORDS;++i) {
        values[0][i]=i%3==0 ? 0x8000 : i%3==1 ? 0x7fff : 0xffff;
        values[1][i]=(uint16_t)(i*269u+17u);
        int32_t a=values[0][i]<32768 ? values[0][i] : (int32_t)values[0][i]-65536;
        int32_t b=values[1][i]<32768 ? values[1][i] : (int32_t)values[1][i]-65536;
        scalar+=(int64_t)a*b;
    }
    for(unsigned bank=0;bank<2;++bank) for(unsigned offset=0;offset<BLOCK_WORDS;offset+=BLOCK_CHUNK) {
        q.opcode=BLOCK_UPLOAD;q.sequence++;q.length=4+2*BLOCK_CHUNK;
        q.payload[0]=(uint8_t)bank;block_put16(q.payload+1,(uint16_t)offset);q.payload[3]=BLOCK_CHUNK;
        for(unsigned i=0;i<BLOCK_CHUNK;++i) block_put16(q.payload+4+2*i,values[bank][offset+i]);
        r=exchange(&m,&q);
        CHECK(r.payload[0]==BLOCK_OK && m.loaded[bank]==offset+BLOCK_CHUNK &&
              !memcmp(m.operands[bank]+offset,values[bank]+offset,BLOCK_CHUNK*sizeof(uint16_t)),
              "full-capacity bank preserves the accepted words in order");
    }
    q.opcode=BLOCK_DOT;q.sequence++;q.length=2;block_put16(q.payload,BLOCK_WORDS);
    r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_OK && m.result==(uint16_t)scalar,
          "1024 signed extreme products match independent wide scalar sum modulo 16 bits");
    q.sequence++;block_put16(q.payload,BLOCK_WORDS+1);r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_ARGUMENT && m.executions==1 && m.result==(uint16_t)scalar,
          "overlong dot cannot alter prior result or execute");
    q.opcode=BLOCK_UPLOAD;q.sequence++;q.length=6;q.payload[0]=0;
    block_put16(q.payload+1,BLOCK_WORDS);q.payload[3]=1;block_put16(q.payload+4,0x1234);
    r=exchange(&m,&q);
    CHECK(r.payload[0]==BLOCK_ARGUMENT && m.loaded[0]==BLOCK_WORDS &&
          m.operands[0][BLOCK_WORDS-1]==values[0][BLOCK_WORDS-1],"upload beyond capacity is atomic rejection");
    for (size_t length=0;length<4;++length) {
        q.sequence++;q.length=length;r=exchange(&m,&q);
        CHECK(r.payload[0]==BLOCK_ARGUMENT && m.loaded[0]==BLOCK_WORDS,
              "short semantic payload rejected before accessing its missing fields");
    }
    q.sequence++;q.length=6;block_put16(q.payload+1,0);
    uint8_t wire[BLOCK_WIRE];size_t n=block_encode(&q,wire,sizeof(wire));
    block_model before;memcpy(&before,&m,sizeof(m));
    block_decoder d={0};block_frame parsed={0};
    for(size_t i=0;i+1<n;++i) {
        if(block_decode(&d,wire[i],&parsed)==1) block_model_request(&m,&parsed,&r);
    }
    CHECK(!memcmp(&before,&m,sizeof(m)),"complete body without closing delimiter commits no effects");
    CHECK(block_decode(&d,wire[n-1],&parsed)==1,"closing delimiter validates the complete request");
    block_model_request(&m,&parsed,&r);
    CHECK(r.payload[0]==BLOCK_OK && m.loaded[0]==1 && m.operands[0][0]==0x1234 && !m.result_valid,
          "validated replacement commits its word/length and invalidates old result");
    q.sequence++;n=block_encode(&q,wire,sizeof(wire));wire[11]^=1;
    memcpy(&before,&m,sizeof(m));memset(&d,0,sizeof(d));int rejected=0;
    for(size_t i=0;i<n;++i) {
        int rc=block_decode(&d,wire[i],&parsed);
        if(rc==1) block_model_request(&m,&parsed,&r);
        if(rc<0) rejected++;
    }
    CHECK(rejected==1 && !memcmp(&before,&m,sizeof(m)),"bad CRC cannot change session/cache/operands/result");
}

int main(void)
{
    framing();resident();boundaries();
    printf("# %u block codec/model checks passed\n",checks);
    return 0;
}
