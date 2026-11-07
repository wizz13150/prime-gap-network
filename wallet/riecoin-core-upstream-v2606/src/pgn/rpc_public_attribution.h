// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <univalue.h>
#include <optional>
#include <string>
#include <string_view>

namespace pgn::core {
// RPC-only projection, never a consensus rule. The caller supplies a disclosed
// report and its immutable root from the authenticated branch. Return only the
// small public label; never expose the report's answers or hiding nonce here.
// Clients still apply their Unicode/display policy to this self-declared name.
template<class Digest>
std::optional<UniValue> DisclosedPublicAttribution(std::string_view disclosure,
        std::string_view expected_root, Digest&& digest) {
    if(disclosure.empty()||disclosure.size()>65536||expected_root.size()!=64)return {};
    if(digest(disclosure)!=expected_root)return {};
    UniValue body;
    if(!body.read(std::string{disclosure})||!body.isObject()
            ||!body["schema"].isStr()||body["schema"].get_str()!="PGN_NETWORK_RANGE_REPORT_V1")return {};
    const auto& label=body["discoverer"];
    if(!label.isObject()||label.getKeys().size()!=2||!label["schema"].isStr()
            ||label["schema"].get_str()!="PGN_PUBLIC_ATTRIBUTION_V1"||!label["name"].isStr())return {};
    const auto& name=label["name"].get_str();
    if(name.empty()||name.size()>192)return {};
    for(const unsigned char c:name){
        if(c<128&&!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
                ||c==' '||c=='.'||c=='_'||c=='\''||c=='-'))return {};
    }
    return label;
}
}
