// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <support/allocators/secure.h>
#include <functional>

namespace interfaces {
using WalletUnlockFn = std::function<bool(const SecureString&)>;
/** A native authorization scope, not a key, passphrase store or frontend owner.
 * Only this unlock function can bind its matching relock to a successful grant.
 * Closing the scope makes delayed unlock attempts fail without authentication. */
struct WalletUnlockScope {
    WalletUnlockFn unlock;
    std::function<void()> relock;
};
} // namespace interfaces
