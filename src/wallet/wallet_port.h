#pragma once

#include <functional>
#include <string>

namespace pgn::wallet {

// The wallet owns network/node work. It cannot call the mining engine, access
// its candidate loop, or demand a per-result digest. UI integration is a port,
// not a shared worker or a shared calculation dependency.
struct Observation {
    std::string network;
    std::string height;
    std::string balance;
    std::string observedUtc;
    bool connected{};
    bool initialSync{};
};

class WalletPort {
public:
    virtual ~WalletPort() = default;
    virtual void observe(std::function<void(Observation)> consume) = 0;
};

} // namespace pgn::wallet
