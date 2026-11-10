// Copyright (c) 2026 The Prime Gap Network developers
// Distributed under the MIT software license, see COPYING.
#include <clientversion.h>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        if (UA_NAME != "PGN-Core") throw std::runtime_error("Native peer identity is not PGN Core");
        const auto subversion=FormatSubVersion(UA_NAME,CLIENT_VERSION,{});
        if (!subversion.starts_with("/PGN-Core:") || subversion.back()!='/' || subversion.find("Stella")!=std::string::npos)
            throw std::runtime_error("Native subversion formatting lost the project identity");
        std::cout<<"{\"PASS\":true,\"peer_identity\":\""<<subversion<<"\",\"network_started\":false}\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
