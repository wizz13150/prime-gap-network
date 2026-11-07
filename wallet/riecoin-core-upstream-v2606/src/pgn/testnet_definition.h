// Copyright (c) 2026 The PGN developers. MIT.
// Isolated PGN testnet definition. Old MAIN data and genesis remain unchanged.
#pragma once
#include <pgn/native_main_parameters.h>
#include <consensus/merkle.h>
#include <util/strencodings.h>

namespace pgn::core {
inline NativeMainParameterDefinition BuildPgnTestnetDefinition(){
    constexpr RangePolicyReadBudget budget{32,512,65536,32768};
    const Integer origin{"150000000000000000000"},width{"200000000000000"};
    const Integer limit=Integer{1}<<95;
    const RangePoolDefinition ranges{origin,width,(limit-origin+width-1)/width,2,144,10000,limit};
    const EncodingLimits records{1000000,2000000,512,100};
    const CommittedGapPolicy evidence{records,{{4096,4096,256},65536},{4096,256,512},{128,256},29,1};
    const ScienceVerificationDefinition science{100,evidence,{65536,2000000,512,100}};
    const CAmount coin=PGN_ATOMIC_UNITS_PER_COIN;
    // Total ordinary budget includes the audit allocation. 342:128:25 of
    // 247.5 gives 171 to gold, 64 across the near-miss pots, and 12.5 to audits.
    const MonetaryPolicyDefinition money{99000000*coin,1000000*coin,{495*coin/2,1},
        495,342,128,25,FeeRoutingDefinition{1,1}};
    const NativeWorkDefinition work{arith_uint256{1}<<32,arith_uint256{1}};
    const MeritControllerDefinition controller{29*65536,16*65536,64*65536,600,172800};
    constexpr uint32_t bootstrap_bits=0x207fffff;
    const auto range_script=BuildCandidateRangePolicyEnvelope(ranges,budget);
    const auto science_script=BuildCandidateSciencePolicyEnvelope(science,budget);
    const auto money_script=BuildCandidateMonetaryPolicyEnvelope(money,budget);
    const auto bootstrap_script=BuildCandidateBootstrapHashPolicyEnvelope(bootstrap_bits,budget);
    const auto work_script=BuildCandidateNativeWorkEnvelope(work,budget);
    const auto controller_script=BuildCandidateNativeRetargetEnvelope(controller,budget);
    if(!range_script||!science_script||!money_script||!bootstrap_script||!work_script||!controller_script)
        throw std::logic_error("PGN testnet policy encoding failed");

    // Genesis commits the detailed era/tier rules before live work is issued.
    // Their state transitions are verified separately from this definition.
    Bytes rewards{1};
    const auto put=[&](uint64_t value,unsigned bytes){
        for(unsigned i=0;i<bytes;++i)rewards.push_back(static_cast<unsigned char>(value>>(8*i)));
    };
    put(200000,4);put(10,4);
    for(const CAmount amount:{171*coin,32*coin,24*coin,8*coin,125*coin,1000*coin})put(amount,8);
    put(40*65536,4);
    const std::string rewards_tag="PGNTESTNET-REWARDS-1";
    CMutableTransaction coinbase;coinbase.vin.resize(1);coinbase.vin[0].prevout.SetNull();
    const std::string identity="PGN TESTNET / prime gaps / reset 2026-11-06";
    coinbase.vin[0].scriptSig=CScript{}<<OP_0<<Bytes{identity.begin(),identity.end()};
    coinbase.vout={{0,*range_script},{0,*science_script},{0,*money_script},{0,*bootstrap_script},
        {0,*work_script},{0,*controller_script},
        {0,CScript{}<<OP_RETURN<<Bytes{rewards_tag.begin(),rewards_tag.end()}<<rewards}};
    CBlock genesis;genesis.nVersion=4;genesis.nTime=1793923200;
    genesis.nBits=bootstrap_bits;genesis.nNonce=NativeBootstrapNonce(0);
    genesis.vtx={MakeTransactionRef(coinbase)};genesis.hashMerkleRoot=BlockMerkleRoot(genesis);
    const auto target=*CanonicalBootstrapHashTarget(bootstrap_bits);
    for(uint64_t counter=0;UintToArith256(genesis.GetHash())>target;++counter){
        if(counter==1000000)throw std::logic_error("PGN testnet genesis bootstrap exhausted");
        // Genesis keeps the canonical zero header counter. The deterministic
        // construction salt belongs to its committed coinbase instead.
        coinbase.vin[0].scriptSig=CScript{}<<OP_0<<Bytes{identity.begin(),identity.end()}<<int64_t(counter+1);
        genesis.vtx[0]=MakeTransactionRef(coinbase);
        genesis.hashMerkleRoot=BlockMerkleRoot(genesis);
    }
    const auto id=genesis.GetHash();
    const auto addresses=PgnTestnetAddressPolicy();
    return {genesis,id,{0xd7,0x54,0x4e,0x32},48941,48942,addresses,
        {Bytes{0x04,0x35,0x87,0xcf},Bytes{0x04,0x35,0x83,0x94}},
        {128},AtomicFeeRatePolicy{coin,{128}},budget};
}
}
