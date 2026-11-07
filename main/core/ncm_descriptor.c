#include "ncm_descriptor.h"
#include "../usb_descriptors.h"
#include <string.h>
/* Match CDC Union association instead of assuming adjacent interface numbers.
 * Select the first NCM function, then bind only its Union-associated data pipes.
 * Multiple independent functions are allowed; duplicate control/alternate is not. */
bool cp_ncm_discover(const uint8_t *p,size_t size,cp_ncm_descriptor *out)
{
    if(!out || !usb_descriptors_walk(p,size,NULL,NULL)) return false;
    cp_ncm_descriptor result={0};
    int control=-1,current=-1,slave=-1; bool selected=false,ethernet=false,united=false;
    for(size_t off=0;off<size;off+=p[off]) {
        const uint8_t *d=p+off;
        if(d[1]==4) {
            current=d[2]; selected=d[5]==2 && d[6]==13 && d[3]==0 && (control<0 || current==control);
            if(selected) {
                if(control>=0) return false;
                control=current; result.control=current;
            }
        } else if(selected && d[1]==0x24) {
            if(d[0]<3) return false;
            if(d[2]==6) {
                if(d[0]!=5 || united || d[3]!=current || d[4]==current) return false;
                slave=d[4]; united=true;
            } else if(d[2]==0x0f) {
                if(d[0]<13 || ethernet || !d[3]) return false;
                result.mac_string=d[3]; ethernet=true;
            }
        } else if(selected && d[1]==5 && (d[3]&3)==3 && (d[2]&128)) {
            if(result.notification) return false;
            result.notification=d[2]; result.notification_mps=(d[4]|d[5]<<8)&2047;
            if(!result.notification_mps || result.notification_mps>1024) return false;
        }
    }
    if(control<0 || !united || !ethernet) return false;
    bool found=false; int endpoints=0; selected=false;
    for(size_t off=0;off<size;off+=p[off]) {
        const uint8_t *d=p+off;
        if(d[1]==4) {
            if(selected && endpoints!=2) return false;
            selected=d[2]==slave && d[5]==10 && d[4]>0;
            endpoints=0;
            if(selected) {
                if(found || d[4]!=2) return false;
                found=true; result.data=d[2]; result.alternate=d[3];
            }
        } else if(selected && d[1]==5) {
            if((d[3]&3)!=2 || !(d[2]&15) || (d[2]&0x70)) return false;
            unsigned mps=(d[4]|d[5]<<8)&2047;
            if(mps!=64 && mps!=512) return false;
            if(d[2]&128) { if(result.in) return false; result.in=d[2]; result.in_mps=mps; }
            else { if(result.out) return false; result.out=d[2]; result.out_mps=mps; }
            ++endpoints;
        }
    }
    if(selected && endpoints!=2) return false;
    if(!found || !result.in || !result.out) return false;
    *out=result; return true;
}
