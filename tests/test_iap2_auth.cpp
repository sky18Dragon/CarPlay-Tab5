#include "core/iap2_auth_state.h"
#include <assert.h>
#include <stdio.h>
int main() {
    using iap2::AuthState;
    AuthState valid;
    assert(valid.certificate_sent() && valid.can_sign());
    assert(valid.signature_sent() && !valid.accepted());
    assert(valid.signature_acknowledged() && valid.accept() && valid.accepted());
    for(unsigned stage=0;stage<3;++stage) {
        AuthState early;
        if(stage>=1) assert(early.certificate_sent());
        if(stage>=2) assert(early.signature_sent());
        assert(!early.accept() && !early.accepted());
        assert(!early.certificate_sent());
    }
    AuthState repeated;assert(repeated.certificate_sent());assert(!repeated.certificate_sent());assert(!repeated.can_sign());
    AuthState rejected;assert(rejected.certificate_sent());assert(rejected.signature_sent());rejected.reject();
    assert(!rejected.signature_acknowledged() && !rejected.accepted());
    puts("iAP2 authentication gate: ACK-before-success, early success, duplicate and rejection passed");
}
