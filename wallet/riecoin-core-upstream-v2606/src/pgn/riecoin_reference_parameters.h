// MIT. Explicit upstream reference fixtures, never selected by PGN startup.
#pragma once
#include <kernel/chainparams.h>
namespace pgn::reference {
std::unique_ptr<const CChainParams> Main(const CChainParams::MainNetOptions&);
inline std::unique_ptr<const CChainParams> Main(){return Main(CChainParams::MainNetOptions{});}
}
