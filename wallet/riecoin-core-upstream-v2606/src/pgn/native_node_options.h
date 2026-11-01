// MIT. Actual node options from immutable selected native genesis.
#pragma once
#include <pgn/native_parameter_work.h>
#include <pgn/native_main_parameters.h>
#include <validation.h>
namespace pgn::core {
inline bool ApplyNativeChainstateProfile(ChainstateManager::Options& options){
    const auto& params=options.chainparams;const auto& consensus=params.GetConsensus();
    if(!consensus.pgn_native_proof)return !params.PgnMonetaryContext();
    if(!params.PgnMonetaryContext()||!params.PgnNativeAddressPolicy()||!params.PgnAtomicFeeRatePolicy()||
       params.IsMockableChain())return false;
    constexpr RangePolicyReadBudget budget{4000000,4096,4000000,4000000};
    const auto& genesis=params.GenesisBlock();const auto rt=ReadPinnedNativeRetarget(genesis,consensus.hashGenesisBlock,budget);
    const auto work=ReadPinnedNativeWorkPolicy(genesis,consensus.hashGenesisBlock,budget);
    const auto boot=ReadPinnedGenesisBootstrapBits(genesis,consensus.hashGenesisBlock,budget);
    const auto& configured=*consensus.pgn_native_proof;
    if(!rt.policy||!work.policy||!boot||*boot!=configured.bootstrap_bits||
       ArithToUint256(work.policy->scientific)!=configured.scientific_unit||
       NativeRetargetPayload(*rt.policy)!=NativeRetargetPayload(NativeParameterController(configured)))return false;
    // Explicit LOCAL reconstruction bounds. Exhaustion is held as a local
    // resource error, never consensus invalidity or a sender subset fallback.
    const RequestHistoryArchiveBudget archive{4000000,1U<<20};
    // Typed timed requests are an explicit testnet protocol capability. Main
    // keeps its original request domain; no miner can choose this node rule.
    const TicketRequestBudget requests{10000,10000,1U<<20,params.IsTestChain()};
    const ConfirmedRequestScanBudget scan{4000000,4000000,4000000,uint32_t(std::numeric_limits<int32_t>::max()),requests};
    const RangeScheduleBudget schedule{4096,262144,262144,archive,scan,{128,4096,4194304}};
    options.pgn_candidate_science_budget=std::make_shared<const NativeBlockScienceBudget>(NativeBlockScienceBudget{budget,schedule,{10000,1U<<20},4000000,4000000,4000000});
    options.pgn_candidate_bootstrap_budget=std::make_shared<const NativeBlockBootstrapBudget>(NativeBlockBootstrapBudget{budget,{4096,262144,archive,scan},4096,params.IsTestChain()});
    options.pgn_candidate_fixed_block_weight=work.policy->scientific;
    options.assumed_valid_block=uint256::ZERO;
    return true;
}
}
