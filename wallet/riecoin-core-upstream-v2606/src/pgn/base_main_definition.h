// Copyright (c) 2026 The PGN developers. MIT.
// Reproducible BASE definition selected by the native MAIN factory.
#pragma once
#include <pgn/native_main_parameters.h>
#include <util/strencodings.h>
namespace pgn::core {
inline NativeMainParameterDefinition BuildBaseMainDefinition(){
    constexpr RangePolicyReadBudget budget{32,512,65536,32768};
    const RangePoolDefinition ranges{Integer{"100000000000000000000"},Integer{"1000000000000000"},1000000,2,144};
    const EncodingLimits records{1000000,2000000,512,256};
    const CommittedGapPolicy evidence{records,{{4096,4096,256},65536},{4096,256,512},{128,256},29,1};
    const ScienceVerificationDefinition science{256,evidence,{65536,2000000,512,256}};
    const CAmount coin=PGN_ATOMIC_UNITS_PER_COIN;
    // WP0.7 illustrative numbers are PROPOSED for this candidate, not prior
    // operator-adopted economics. No mainnet/approval token is created here.
    const MonetaryPolicyDefinition money{99000000*coin,1000000*coin,{200*coin,1},100,70,25,5,FeeRoutingDefinition{1,1}};
    const NativeWorkDefinition work{arith_uint256{1}<<32,arith_uint256{1}};
    const MeritControllerDefinition controller{29*65536,16*65536,64*65536,600,172800};
    const auto rangeScript=BuildCandidateRangePolicyEnvelope(ranges,budget);
    const auto scienceScript=BuildCandidateSciencePolicyEnvelope(science,budget);
    const auto moneyScript=BuildCandidateMonetaryPolicyEnvelope(money,budget);
    const auto bootScript=BuildCandidateBootstrapHashPolicyEnvelope(0x1d00ffff,budget);
    const auto workScript=BuildCandidateNativeWorkEnvelope(work,budget);
    const auto controllerScript=BuildCandidateNativeRetargetEnvelope(controller,budget);
    if(!rangeScript||!scienceScript||!moneyScript||!bootScript||!workScript||!controllerScript)
        throw std::logic_error("BASE candidate policy encoding failed");
    CMutableTransaction cb;cb.vin.resize(1);cb.vin[0].prevout.SetNull();
    const std::string text="PGN / useful prime gaps / BASE / 2026-10-16";
    cb.vin[0].scriptSig=CScript{}<<OP_0<<Bytes{text.begin(),text.end()};
    const auto document=ParseHex("e1659349b1973ad65e88062a15e1b1db0ad6a51ffe214d6e988e81fd3161cffd");
    const std::string tag="PGNWHITEPAPER-SHA256-1";
    cb.vout={{0,*rangeScript},{0,*scienceScript},{0,*moneyScript},{0,*bootScript},{0,*workScript},{0,*controllerScript},
        {0,CScript{}<<OP_RETURN<<Bytes{tag.begin(),tag.end()}<<document}};
    CBlock genesis;genesis.nVersion=4;genesis.nTime=1792194000; // 2026-10-16T23:40:00Z
    genesis.nBits=0x1d00ffff;genesis.nNonce=NativeBootstrapNonce(0);genesis.vtx={MakeTransactionRef(cb)};
    genesis.hashMerkleRoot=BlockMerkleRoot(genesis);
    const uint256 id{"f3275aa518624618ffc7cb9e81b54568414f6a80df09b7c7a1dd4508bf7f6fd0"};
    if(genesis.GetHash()!=id)throw std::logic_error("Compiled BASE genesis identity changed");
    return {genesis,id,{0xd7,0xa6,0x91,0xf4},48731,48732,
        BasePgnAddressPolicy(),
        {Bytes{0x04,0x88,0xb2,0x1e},Bytes{0x04,0x88,0xad,0xe4}},{128},AtomicFeeRatePolicy{coin,{128}},budget};
}
}
