#include "iap2_params.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int main() {
    using namespace iap2;
    Body<16> b; CHECK(b.u16(0,0x1234)); CHECK(b.u8(2,1)); CHECK(b.ok());
    Parameters cursor(b.data(),b.size()); Parameter p;
    CHECK(cursor.next(p) && p.id==0 && p.size==2 && be16(p.data)==0x1234);
    CHECK(cursor.next(p) && p.id==2 && p.size==1 && p.data[0]==1);
    CHECK(!cursor.next(p) && cursor.complete());
    CHECK(parameter_zero(b.data(),b.size(),p) && p.size==2);
    Body<32> duplicate; duplicate.u16(0,1); duplicate.u16(0,2);
    CHECK(!parameter_zero(duplicate.data(),duplicate.size(),p));
    CHECK(!parameter_zero(b.data(),b.size()-1,p));
    uint8_t short_length[]={0,3,0,0}; CHECK(!parameter_zero(short_length,4,p));
    uint8_t too_long[]={0,255,0,0}; CHECK(!parameter_zero(too_long,4,p));
    CHECK(!parameter_zero(nullptr,5,p)); CHECK(!parameter_zero(nullptr,0,p));
    Body<4> tiny; CHECK(tiny.add(1,nullptr,0)); CHECK(!tiny.u8(2,1) && !tiny.ok());
    Body<4> empty; CHECK(!empty.add(0,nullptr,1));
    Body<8> text; CHECK(text.text(1,"abc") && text.size()==8);
    Body<8> port; CHECK(port.u32(2,7000) && port.size()==8);
    CHECK(port.data()[4]==0 && port.data()[5]==0 && be16(port.data()+6)==7000);
    return 0;
}
