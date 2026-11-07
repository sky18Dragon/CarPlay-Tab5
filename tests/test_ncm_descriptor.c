#include "core/ncm_descriptor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t config[]={
9,2,0,0,2,1,0,0x80,50,
9,4,3,0,1,2,13,0,0,
5,0x24,6,3,7,
13,0x24,0x0f,4,0,0,0,0,0xea,5,0,0,0,
7,5,0x81,3,16,0,9,
9,4,7,0,0,10,0,0,0,
9,4,7,1,2,10,0,0,0,
7,5,0x82,2,0,2,0,
7,5,0x03,2,0,2,0};
int main(void) {
    config[2]=sizeof(config);
    cp_ncm_descriptor d;
    assert(cp_ncm_discover(config,sizeof(config),&d));
    assert(d.control==3 && d.data==7 && d.alternate==1 && d.in_mps==512 && d.out_mps==512);
    for(size_t i=0;i<sizeof(config);++i) assert(!cp_ncm_discover(config,i,&d));
    uint8_t copy[sizeof(config)];memcpy(copy,config,sizeof(copy));
    copy[22]=8;assert(!cp_ncm_discover(copy,sizeof(copy),&d)); /* union wrong slave */
    memcpy(copy,config,sizeof(copy));copy[sizeof(copy)-3]=64;copy[sizeof(copy)-2]=0;
    assert(cp_ncm_discover(copy,sizeof(copy),&d) && d.out_mps==64);
    copy[sizeof(copy)-3]=32;assert(!cp_ncm_discover(copy,sizeof(copy),&d));
    memcpy(copy,config,sizeof(copy));copy[sizeof(copy)-5]=0x82;
    assert(!cp_ncm_discover(copy,sizeof(copy),&d)); /* duplicate IN */
    memcpy(copy,config,sizeof(copy));copy[26]=0;
    assert(!cp_ncm_discover(copy,sizeof(copy),&d)); /* missing MAC */
    uint8_t dual[sizeof(config)*2-9];
    memcpy(dual,config,sizeof(config)); memcpy(dual+sizeof(config),config+9,sizeof(config)-9);
    dual[2]=sizeof(dual);dual[3]=0;dual[4]=4;
    for(size_t off=sizeof(config);off<sizeof(dual);off+=dual[off]) {
        if(dual[off+1]==4) dual[off+2]=dual[off+2]==3?9:10;
        if(dual[off+1]==0x24 && dual[off+2]==6) { dual[off+3]=9;dual[off+4]=10; }
    }
    assert(cp_ncm_discover(dual,sizeof(dual),&d) && d.control==3 && d.data==7);
    dual[sizeof(config)+2]=3; /* duplicate first control remains ambiguous */
    assert(!cp_ncm_discover(dual,sizeof(dual),&d));
    puts("NCM descriptor: union association, HS/FS, truncation and invalid endpoints passed");
}
