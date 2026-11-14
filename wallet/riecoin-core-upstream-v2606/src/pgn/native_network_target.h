// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/native_elapsed_retarget.h>
#include <pgn/native_hourly_activation.h>
#include <pgn/native_hourly_consensus_quote.h>
namespace pgn::core {
struct NativeNetworkTarget {
    uint32_t header_q16,scientific_count;
    Integer numerator,denominator;
    bool hourly;
};
// Read-only presentation of the same threshold used by native admission.
// A sealed, branch-bound history is required for every activated sample.
inline std::optional<NativeNetworkTarget> NativeNetworkTargetAt(
    const CBlockIndex& parent,const MeritControllerDefinition& policy,
    const pgn::discussion::NativeHourlyHistory* history=nullptr)
{
    AssertLockHeld(cs_main);
    const auto* genesis=parent.GetAncestor(0);
    if(!genesis||!genesis->phashBlock)return {};
    const auto activation=ConfiguredNativeHourlyMerit(genesis->GetBlockHash());
    if(activation&&(activation->genesis!=genesis->GetBlockHash()||activation->median_time<0||activation->median_time%3600))return {};
    const auto legacy=DeriveNextMeritTarget(parent,policy);
    if(!legacy)return {};
    if(activation&&parent.GetMedianTimePast()>=activation->median_time){
        if(!history)return {};
        const auto quote=history->QuoteAt(parent.nHeight,parent.GetBlockHash(),policy.maximum_q16);
        if(!quote.quote)return {};
        return NativeNetworkTarget{quote.quote->target.header_q16,legacy->scientific_count,
            quote.quote->target.proof_numerator,quote.quote->target.proof_denominator,true};
    }
    return NativeNetworkTarget{legacy->target_q16,legacy->scientific_count,Integer{legacy->target_q16},Integer{65536},false};
}
}
