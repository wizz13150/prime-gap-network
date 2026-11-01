#pragma once

#include <string>
#include <string_view>

inline std::string FrontierDeviceProvenanceLabel(std::string_view name,
                                                 std::string_view deviceId)
{
    const std::string shortId = deviceId.empty()
        ? std::string{}
        : std::string(deviceId.substr(deviceId.size() > 8 ? deviceId.size() - 8 : 0));
    if (!name.empty() && !shortId.empty()) {
        return std::string(name) + " / ..." + shortId;
    }
    if (!name.empty()) return std::string(name);
    if (!shortId.empty()) return std::string("DEVICE ...") + shortId;
    return "DEVICE UNKNOWN";
}

