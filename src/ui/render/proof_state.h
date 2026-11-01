#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

enum class FrontierProofRank { Probable = 0, Rejected = 1, Exact = 2 };

inline FrontierProofRank FrontierProofStateRank(std::string_view state)
{
    std::string lower(state);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower.find("reject") != std::string::npos) return FrontierProofRank::Rejected;
    if (lower.find("pending") != std::string::npos ||
        lower.find("probable") != std::string::npos ||
        lower.find("unverified") != std::string::npos ||
        lower.find("not_verified") != std::string::npos ||
        lower.find("not-verified") != std::string::npos) return FrontierProofRank::Probable;
    if (lower.find("exact") != std::string::npos ||
        lower.find("certified") != std::string::npos ||
        lower.find("verified") != std::string::npos) return FrontierProofRank::Exact;
    return FrontierProofRank::Probable;
}

inline bool FrontierProofRevisionDominates(std::string_view previous,
                                           std::string_view incoming)
{
    return static_cast<int>(FrontierProofStateRank(incoming)) >
        static_cast<int>(FrontierProofStateRank(previous));
}

struct FrontierPhysicalGapIdentity {
    std::string runId;
    std::string leftIndex;
    std::string rightIndex;
    std::string leftEndpoint;
    std::string rightEndpoint;
    std::string candidateId;
};

inline bool FrontierSamePhysicalGap(const FrontierPhysicalGapIdentity& left,
                                    const FrontierPhysicalGapIdentity& right)
{
    const bool sameEndpoints = !left.leftEndpoint.empty() && !left.rightEndpoint.empty() &&
        left.leftEndpoint == right.leftEndpoint && left.rightEndpoint == right.rightEndpoint;
    if (sameEndpoints) return true;

    // Global indices are only meaningful inside the producer run that owns
    // them.  Endpoints and stable candidate ids, on the other hand, identify
    // the same mathematical gap even when it is mirrored, verified, or
    // replayed through another telemetry stream.
    if (left.runId.empty() || left.runId != right.runId) return false;
    const bool sameIndices = !left.leftIndex.empty() && !left.rightIndex.empty() &&
        left.leftIndex == right.leftIndex && left.rightIndex == right.rightIndex;
    const bool sameCandidate = !left.candidateId.empty() &&
        left.candidateId == right.candidateId;
    return sameIndices || sameCandidate;
}
