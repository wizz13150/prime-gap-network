// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsbase.h>
#include <common/args.h>
#include <consensus/params.h>
#include <crypto/common.h>
#include <deploymentinfo.h>
#include <tinyformat.h>
#include <util/chaintype.h>
#include <util/log.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using util::SplitString;

static void HandleDeploymentArgs(const ArgsManager& args, CChainParams::DeploymentOptions& options)
{

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams = SplitString(strDeployment, ':');
        if (vDeploymentParams.size() < 3 || 4 < vDeploymentParams.size()) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end[:min_activation_height]");
        }
        CChainParams::VersionBitsParameters vbparams{};
        const auto start_time{ToIntegral<int64_t>(vDeploymentParams[1])};
        if (!start_time) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        vbparams.start_time = *start_time;
        const auto timeout{ToIntegral<int64_t>(vDeploymentParams[2])};
        if (!timeout) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        vbparams.timeout = *timeout;
        if (vDeploymentParams.size() >= 4) {
            const auto min_activation_height{ToIntegral<int64_t>(vDeploymentParams[3])};
            if (!min_activation_height) {
                throw std::runtime_error(strprintf("Invalid min_activation_height (%s)", vDeploymentParams[3]));
            }
            vbparams.min_activation_height = *min_activation_height;
        } else {
            vbparams.min_activation_height = 0;
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                options.version_bits_parameters[Consensus::DeploymentPos(j)] = vbparams;
                found = true;
                LogInfo("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, min_activation_height=%d",
                        vDeploymentParams[0], vbparams.start_time, vbparams.timeout, vbparams.min_activation_height);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

void ReadMainNetArgs(const ArgsManager& args, CChainParams::MainNetOptions& options)
{
    HandleDeploymentArgs(args, options.dep_opts);
}

void ReadTestNetArgs(const ArgsManager& args, CChainParams::TestNetOptions& options)
{
    HandleDeploymentArgs(args, options.dep_opts);
}

void ReadRegTestArgs(const ArgsManager& args, CChainParams::RegTestOptions& options)
{
    if (auto value = args.GetBoolArg("-fastprune")) options.fastprune = *value;

    HandleDeploymentArgs(args, options.dep_opts);
}

static std::unique_ptr<const CChainParams> globalChainParams;

std::optional<pgn::core::MonetaryContext> SelectedPgnMonetaryContext()
{
    return globalChainParams ? globalChainParams->PgnMonetaryContext() : std::nullopt;
}

std::optional<pgn::core::AtomicFeeRatePolicy> SelectedPgnAtomicFeeRatePolicy()
{
    return globalChainParams ? globalChainParams->PgnAtomicFeeRatePolicy() : std::nullopt;
}
std::optional<pgn::core::NativeAddressPolicy> SelectedPgnNativeAddressPolicy()
{
    return globalChainParams ? globalChainParams->PgnNativeAddressPolicy() : std::nullopt;
}

std::optional<Consensus::AmountRules> SelectedPgnConsensusAmountRules()
{
    return globalChainParams ? globalChainParams->GetConsensus().amount_rules : std::nullopt;
}

std::optional<uint32_t> SelectedChainDatabaseApplicationId()
{
    if (!globalChainParams) return std::nullopt;
    return ReadBE32(globalChainParams->MessageStart().data());
}

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const ChainType chain)
{
    std::unique_ptr<const CChainParams> result;
    switch (chain) {
    case ChainType::MAIN: {
        auto opts = CChainParams::MainNetOptions{};
        ReadMainNetArgs(args, opts);
        result = CChainParams::Main(opts);
        break;
    }
    case ChainType::TESTNET: {
        auto opts = CChainParams::TestNetOptions{};
        ReadTestNetArgs(args, opts);
        result = CChainParams::TestNet(opts);
        break;
    }
    case ChainType::REGTEST: {
        auto opts = CChainParams::RegTestOptions{};
        ReadRegTestArgs(args, opts);
        result = CChainParams::RegTest(opts);
        break;
    }
    }
    if (!result) throw std::runtime_error("Unknown chain parameter type");
    // Check the REAL codec before the caller can publish immutable parameters.
    // Inherited factories still have absent PGN domains; that grants nothing.
    result->CheckPgnParameterCoherence();
    return result;
}

void SelectParams(const ChainType chain)
{
    auto selected = CreateChainParams(gArgs, chain);
    SelectBaseParams(chain);
    globalChainParams = std::move(selected);
}
