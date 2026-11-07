// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace iap2 {
inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0])<<8|p[1]; }
inline void put16(uint8_t *p,uint16_t v) { p[0]=uint8_t(v>>8); p[1]=uint8_t(v); }
struct Parameter { uint16_t id; const uint8_t *data; size_t size; };
class Parameters {
    const uint8_t *p;
    size_t left;
    bool valid=true;
public:
    Parameters(const uint8_t *data,size_t size):p(data),left(size) { if (!data && size) valid=false; }
    bool next(Parameter &out) {
        if (!valid || !left) return false;
        if (left<4) { valid=false; return false; }
        size_t length=be16(p);
        if (length<4 || length>left) { valid=false; return false; }
        out={be16(p+2),p+4,length-4}; p+=length; left-=length; return true;
    }
    bool complete() const { return valid && !left; }
};
template<size_t Capacity=768> class Body {
    uint8_t bytes[Capacity];
    size_t length=0;
    bool valid=true;
public:
    bool add(uint16_t id,const void *data,size_t size) {
        if (!valid || size>65531 || length>Capacity || Capacity-length<4 || size>Capacity-length-4 || (!data && size)) return valid=false;
        put16(bytes+length,uint16_t(size+4)); put16(bytes+length+2,id);
        if (size) memcpy(bytes+length+4,data,size);
        length+=size+4; return true;
    }
    bool text(uint16_t id,const char *text) { return text?add(id,text,strlen(text)+1):(valid=false); }
    bool u8(uint16_t id,uint8_t value) { return add(id,&value,1); }
    bool u16(uint16_t id,uint16_t value) { uint8_t p[2]; put16(p,value); return add(id,p,2); }
    bool u32(uint16_t id,uint32_t value) { uint8_t p[4]; put16(p,uint16_t(value>>16)); put16(p+2,uint16_t(value)); return add(id,p,4); }
    bool list(uint16_t id,const uint16_t *values,size_t count) {
        if (count>32) return valid=false;
        uint8_t packed[64]; for (size_t i=0;i<count;++i) put16(packed+2*i,values[i]);
        return add(id,packed,count*2);
    }
    template<size_t N> bool group(uint16_t id,const Body<N> &body) { return body.ok()?add(id,body.data(),body.size()):(valid=false); }
    bool ok() const { return valid; }
    const uint8_t *data() const { return bytes; }
    size_t size() const { return length; }
};
// Require one and only one parameter 0; reject malformed tails and duplicates.
inline bool parameter_zero(const uint8_t *body,size_t size,Parameter &out) {
    Parameters cursor(body,size); Parameter p; bool found=false;
    while (cursor.next(p)) {
        if (p.id==0) { if (found) return false; found=true; out=p; }
    }
    return cursor.complete() && found;
}
}
