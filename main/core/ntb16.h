// SPDX-License-Identifier: GPL-3.0-or-later
// NTB16 wire layout follows the local DiPlay Ntb16Codec; USB MPS is supplied.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace ncm {
inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0])|uint16_t(p[1])<<8; }
inline uint32_t le32(const uint8_t *p) { return uint32_t(le16(p))|uint32_t(le16(p+2))<<16; }
inline void put16(uint8_t *p,uint16_t n) { p[0]=uint8_t(n); p[1]=uint8_t(n>>8); }
inline void put32(uint8_t *p,uint32_t n) { put16(p,uint16_t(n)); put16(p+2,uint16_t(n>>16)); }
inline size_t build(uint8_t *out,size_t capacity,const uint8_t *frame,size_t size,uint16_t sequence,unsigned mps) {
    if (!out || !frame || size<14 || size>1514 || !mps) return 0;
    size_t block=size+28, wire=block+(block%mps==0?1:0);
    if (capacity<wire) return 0;
    memset(out,0,28); put32(out,0x484d434e); put16(out+4,12); put16(out+6,sequence);
    put16(out+8,uint16_t(block)); put16(out+10,12);
    put32(out+12,0x304d434e); put16(out+16,16); put16(out+20,28); put16(out+22,uint16_t(size));
    memcpy(out+28,frame,size); if (wire>block) out[block]=0;
    return wire;
}
using FrameCallback=bool (*)(const uint8_t *,size_t,void *);
// Validate the whole table chain before delivering any frame. CRC/NTB32 are not supported.
inline bool parse(const uint8_t *block,size_t size,FrameCallback callback,void *ctx) {
    if (!block || size<28 || size>65535 || le32(block)!=0x484d434e || le16(block+4)!=12 || le16(block+8)!=size) return false;
    struct Datagram { uint16_t index,size; } frames[32];
    struct Table { uint16_t start,end; } tables[8];
    unsigned count=0,hops=0;
    size_t next=le16(block+10);
    if (!next) return false;
    while (next) {
        if (hops==8 || next<12 || next>size-12 || le32(block+next)!=0x304d434e) return false;
        size_t length=le16(block+next+4);
        if (length<12 || length%4 || length>size-next) return false;
        for (unsigned i=0;i<hops;++i) if (next<tables[i].end && next+length>tables[i].start) return false;
        tables[hops++]={uint16_t(next),uint16_t(next+length)};
        bool terminated=false;
        for (size_t pos=next+8;pos<next+length;pos+=4) {
            uint16_t index=le16(block+pos),n=le16(block+pos+2);
            if (!index && !n) { terminated=true; break; }
            if (!index || n<14 || n>1514 || index<12 || index>size || n>size-index || count==32) return false;
            frames[count++]={index,n};
        }
        if (!terminated) return false;
        next=le16(block+next+6);
    }
    for (unsigned i=0;i<count;++i) {
        size_t start=frames[i].index,end=start+frames[i].size;
        for (unsigned j=0;j<hops;++j) if (start<tables[j].end && end>tables[j].start) return false;
        for (unsigned j=0;j<i;++j) if (start<size_t(frames[j].index)+frames[j].size && end>frames[j].index) return false;
    }
    for (unsigned i=0;i<count;++i) if (callback && !callback(block+frames[i].index,frames[i].size,ctx)) return false;
    return true;
}
class Stream {
    uint8_t *buffer;
    size_t capacity,used=0;
    unsigned mps;
    bool padding=false;
public:
    Stream(uint8_t *storage,size_t capacity,unsigned mps):buffer(storage),capacity(capacity),mps(mps) {}
    bool feed(const uint8_t *p,size_t size,FrameCallback callback,void *ctx) {
        if ((!p && size) || !buffer || capacity<65535 || !mps) return false;
        while (size) {
            // A device may terminate an exact-MPS block with a ZLP or an extra
            // zero byte. Accept either; do not consume the next NTH signature.
            if (padding) { padding=false; if (!*p) { ++p; --size; continue; } }
            size_t target=used<12?12:le16(buffer+8);
            if (target<12 || target>capacity || target<used) return false;
            size_t n=target-used; if (n>size) n=size;
            memcpy(buffer+used,p,n); used+=n; p+=n; size-=n;
            if (used<12) continue;
            size_t block=le16(buffer+8);
            if (le32(buffer)!=0x484d434e || le16(buffer+4)!=12 || block<28 || block>capacity) return false;
            if (used<block) continue;
            if (!parse(buffer,block,callback,ctx)) return false;
            padding=block%mps==0; used=0;
        }
        return true;
    }
    void reset() { used=0; padding=false; }
};
}
