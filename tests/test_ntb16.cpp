#include "ntb16.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint8_t storage[65535],frame[1514],block[1600],other[1600];
static unsigned seen;
static size_t expected;
static bool receive(const uint8_t *p,size_t n,void *) { if (n!=expected || memcmp(p,frame,n)) return false; ++seen; return true; }
int main() {
    for (size_t i=0;i<sizeof(frame);++i) frame[i]=uint8_t(i);
    const size_t lengths[]={14,36,484,1514};
    for (size_t bytes:lengths) {
        expected=bytes;
        size_t wire=ncm::build(block,sizeof(block),frame,bytes,42,64); CHECK(wire);
        CHECK(ncm::le16(block+6)==42 && ncm::le16(block+8)==bytes+28);
        // Every fragmentation point, including before and after an optional pad.
        for (size_t split=0;split<=wire;++split) {
            ncm::Stream stream(storage,sizeof(storage),64); seen=0;
            CHECK(stream.feed(block,split,receive,nullptr)); CHECK(stream.feed(block+split,wire-split,receive,nullptr)); CHECK(seen==1);
        }
        ncm::Stream stream(storage,sizeof(storage),64); seen=0;
        for (size_t i=0;i<wire;++i) CHECK(stream.feed(block+i,1,receive,nullptr));
        CHECK(stream.feed(block,wire,receive,nullptr) && seen==2);
        if ((bytes+28)%64==0) {
            stream.reset(); seen=0;
            CHECK(stream.feed(block,wire-1,receive,nullptr)); // Exact USB boundary ending in a ZLP instead.
            CHECK(stream.feed(block,wire,receive,nullptr)); CHECK(seen==2);
        }
    }
    expected=36; size_t wire=ncm::build(block,sizeof(block),frame,expected,0,64);
    CHECK(!ncm::build(other,10,frame,expected,0,64));
    CHECK(!ncm::build(other,sizeof(other),frame,1515,0,64));
    for (unsigned scenario=0;scenario<5;++scenario) {
        memcpy(other,block,wire);
        if (scenario==0) ncm::put16(other+18,12); // NDP cycle.
        if (scenario==1) ncm::put16(other+20,12); // Datagram overlaps its NDP table.
        if (scenario==2) ncm::put16(other+22,65535); // Out-of-bounds datagram.
        if (scenario==3) ncm::put16(other+16,15); // Invalid table length.
        if (scenario==4) ncm::put32(other+12,0x314d434e); // Unsupported CRC form.
        seen=0; CHECK(!ncm::parse(other,64,receive,nullptr) && seen==0);
    }
    return 0;
}
