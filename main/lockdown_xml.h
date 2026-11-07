// SPDX-License-Identifier: GPL-3.0-or-later
// Extracted from the upstream Lockdown client for focused host tests.
#pragma once
#include <string>
namespace lockdown_xml {
inline bool whitespace(char c) { return c==' ' || c=='\n' || c=='\r' || c=='\t'; }
inline bool value_position(const std::string &xml,const char *key,size_t &pos) {
    std::string needle=std::string("<key>")+key+"</key>";
    pos=xml.find(needle);
    if (pos==std::string::npos) return false;
    pos+=needle.size();
    while (pos<xml.size() && whitespace(xml[pos])) ++pos;
    return pos<xml.size();
}
inline bool text_value(const std::string &xml,const char *key,const char *type,std::string &out) {
    size_t pos;
    if (!value_position(xml,key,pos)) return false;
    std::string open=std::string("<")+type+">", close=std::string("</")+type+">";
    if (xml.compare(pos,open.size(),open)!=0) return false;
    pos+=open.size(); size_t end=xml.find(close,pos);
    if (end==std::string::npos) return false;
    out=xml.substr(pos,end-pos); return true;
}
inline bool boolean_value(const std::string &xml,const char *key,bool &out) {
    size_t pos; if (!value_position(xml,key,pos)) return false;
    if (xml.compare(pos,7,"<true/>")==0 || xml.compare(pos,8,"<true />")==0) { out=true; return true; }
    if (xml.compare(pos,8,"<false/>")==0 || xml.compare(pos,9,"<false />")==0) { out=false; return true; }
    return false;
}
inline std::string error_value(const std::string &xml) {
    size_t pos; if (!value_position(xml,"Error",pos)) return {};
    std::string text;
    if (!text_value(xml,"Error","string",text) || text.empty() || text.size()>64) return "UnrecognizedRemoteError";
    for (char c:text) if (!((c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_')) return "UnrecognizedRemoteError";
    return text;
}
}
