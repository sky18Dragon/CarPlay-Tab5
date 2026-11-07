#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
#include "lockdown_reply.h"
static std::vector<uint8_t> packet(const char *s) {
 size_t n=strlen(s); std::vector<uint8_t> b(n+4); b[0]=n>>24;b[1]=n>>16;b[2]=n>>8;b[3]=n;memcpy(b.data()+4,s,n);return b;
}
int main() {
 const auto ok=packet("<plist version=\"1.0\"><dict><key>Type</key><string>com.apple.mobile.lockdown</string></dict></plist>");
 for(size_t cut=0;cut<ok.size();cut++) {
  lockdown::QueryTypeReply r;
  assert(r.append(ok.data(),cut)==lockdown::QueryTypeReply::Result::incomplete);
  assert(r.append(ok.data()+cut,ok.size()-cut)==lockdown::QueryTypeReply::Result::complete);
  assert(r.is_lockdown_service());
 }
 lockdown::QueryTypeReply r;
 auto err=packet("<plist><dict><key>Error</key><string>Denied</string><key>Type</key><string>com.apple.mobile.lockdown</string></dict></plist>");
 r.append(err.data(),err.size());assert(!r.is_lockdown_service());r.reset();
 uint8_t huge[]={0,0,32,1};assert(r.append(huge,4)==lockdown::QueryTypeReply::Result::invalid);r.reset();
 uint8_t zero[4]={};assert(r.append(zero,4)==lockdown::QueryTypeReply::Result::invalid);r.reset();
 auto extra=ok;extra.push_back(0);assert(r.append(extra.data(),extra.size())==lockdown::QueryTypeReply::Result::invalid);
 puts("PASS: Lockdown all fragment splits, explicit error, oversize/zero/trailing bytes");
}
