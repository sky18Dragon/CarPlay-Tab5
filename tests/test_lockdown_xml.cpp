#include <cassert>
#include <cstdio>
#include "lockdown_xml.h"
int main() {
 using namespace lockdown_xml;
 bool b=false;
 assert(boolean_value("<key>EnableSessionSSL</key>\n <true/>","EnableSessionSSL",b) && b);
 assert(boolean_value("<key>EnableSessionSSL</key><false />","EnableSessionSSL",b) && !b);
 assert(!boolean_value("<key>EnableSessionSSL</key><string>true</string>","EnableSessionSSL",b));
 std::string s;
 assert(text_value("<key>Request</key> <string>Pair</string>","Request","string",s) && s=="Pair");
 assert(!text_value("<key>Request</key><string>Pair","Request","string",s));
 assert(error_value("<key>Error</key><string>UserDeniedPairing</string>")=="UserDeniedPairing");
 assert(error_value("<key>Error</key><string>secret\ntext</string>")=="UnrecognizedRemoteError");
 assert(error_value("<key>Error</key><integer>1</integer>")=="UnrecognizedRemoteError");
 assert(error_value("<key>Error</key><string></string>")=="UnrecognizedRemoteError");
 puts("PASS: TLS boolean type, Pair operation, missing close tag, bounded sanitized errors");
}
