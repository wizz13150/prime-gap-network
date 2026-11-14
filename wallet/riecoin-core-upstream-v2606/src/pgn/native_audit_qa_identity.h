// Synthetic QA network identity; never a CLI or RPC consensus override.
#pragma once
#include <uint256.h>
namespace pgn::core {
inline const uint256& NativeAuditQaGenesis(){static const auto value=*uint256::FromHex("26ac7be96030ed2f68ee531f22c0f59ccd380b734d5cc8bc5fab89065161439a");return value;}
inline const uint256& NativeAuditQaMonetaryPolicy(){static const auto value=*uint256::FromHex("a5235150180bfefe4a90dbfcc0c82a821707839177118e0d070ca497a6fc500a");return value;}
}
