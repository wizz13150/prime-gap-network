// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/native_hourly_activation.h>
#include <pgn/native_hourly_consensus_quote.h>
namespace pgn::core {
// Index loading cannot authenticate body-derived thresholds before coins and
// native archives exist. This check consumes an authenticated historical prefix.
inline bool NativeLoadedHourlyHeaderMatches(const CBlockIndex& candidate,
    const pgn::discussion::NativeHourlyHistory& history)
{
    AssertLockHeld(cs_main);
    if(!candidate.pprev||!candidate.phashBlock||candidate.nNonce!=0||
       int64_t(candidate.pprev->nHeight)+1!=candidate.nHeight)return false;
    const auto quote=history.QuoteAt(candidate.pprev->nHeight,candidate.pprev->GetBlockHash());
    return quote.quote&&quote.quote->target.header_q16==candidate.nBits;
}
}
