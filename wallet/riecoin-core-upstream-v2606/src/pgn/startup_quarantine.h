// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.

#pragma once
#include <cstdio>
#include <cstdlib>

namespace pgn::core {
// Diagnostic refusal for an unconfigured selection. Real initialization now
// requires the complete immutable native MAIN profile in AppInitBasicSetup;
// there is no CLI/config approval boolean or inherited consensus fallback.
inline int RefuseUnconfiguredStartup() {
  std::fputs("PGN startup quarantine: consensus not configured.\n"
             "Core startup is disabled for this unconfigured fork; no CLI/config "
             "option grants approval.\n", stderr);
  return EXIT_FAILURE;
}
} // namespace pgn::core
