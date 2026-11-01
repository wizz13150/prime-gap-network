#undef NDEBUG
#include <pgn/candidate_range_reader.h>
#include <pgn/native_report_commitment.h>
#include <coins.h>
#include <chain.h>
#include <consensus/merkle.h>
#include <validation.h>
#include <cassert>
#include <deque>
#include <memory>
#include <vector>
using namespace pgn::core;
namespace {
struct View:CCoinsView {
    FactHead head;
    std::optional<Coin> GetCoin(const COutPoint&)const override{return {};}
    std::optional<Coin> PeekCoin(const COutPoint&)const override{return {};}
    bool HaveCoin(const COutPoint&)const override{return false;}
    uint256 GetBestBlock()const override{return head.tip;}
    std::optional<FactHead> GetPgnFactHead()const override{return head;}
    std::vector<uint256> GetHeadBlocks()const override{return {};}
    void BatchWrite(CoinsViewCacheCursor&,const uint256&)override{assert(false);}
    std::unique_ptr<CCoinsViewCursor> Cursor()const override{return {};}
    size_t EstimateSize()const override{return 0;}
};
constexpr RequestHistoryArchiveBudget archive_budget{4000000,1U<<20};
constexpr RangePolicyReadBudget policy_budget{128,4096,4000000,4000000};
const RangeScheduleBudget schedule_budget{6000,262144,262144,archive_budget,
    {4000000,4000000,4000000,4000000,{10000,10000,1U<<20,true}}, {128,4096,4194304}};
struct Fixture {
    std::deque<uint256> hashes;
    std::vector<std::unique_ptr<CBlockIndex>> blocks;
    View view;
    NativeRequestHistoryArchive archive;
    explicit Fixture(const fs::path& path):archive(path,1U<<20){}
    CBlockIndex& Append(CBlockIndex* previous,unsigned unique){
        const auto height=previous?previous->nHeight+1:0;
        CMutableTransaction transaction;transaction.vin.resize(1);transaction.vin[0].scriptSig=CScript{}<<int64_t(unique);
        CScript script;
        if(!previous){
            const auto policy=BuildCandidateRangePolicyEnvelope({Integer{3},Integer{100},Integer{100000},1,20,10000,Integer{10000003}},policy_budget);
            assert(policy);script=*policy;
        }else script=CScript{}<<OP_RETURN;
        transaction.vout.emplace_back(0,script);
        CBlock block;block.nVersion=4;block.nTime=1000+unique;
        if(previous)block.hashPrevBlock=previous->GetBlockHash();
        block.vtx.push_back(MakeTransactionRef(std::move(transaction)));block.hashMerkleRoot=BlockMerkleRoot(block);
        hashes.push_back(block.GetHash());auto index=std::make_unique<CBlockIndex>(block);
        index->phashBlock=&hashes.back();index->pprev=previous;index->nHeight=height;
        index->nStatus=BLOCK_VALID_SCRIPTS;index->nTx=1;index->BuildSkip();
        auto& current=*index;blocks.push_back(std::move(index));
        if(!previous)view.head={current.GetBlockHash(),current.GetBlockHash()};
        else view.head.tip=current.GetBlockHash();
        const auto stored=archive.StoreBlock(view,current,height,block,archive_budget);
        if(stored!=RequestArchiveVerdict::Stored&&stored!=RequestArchiveVerdict::AlreadyStored)
            std::fprintf(stderr,"Archive fixture height %d verdict %d\n",height,int(stored));
        assert(stored==RequestArchiveVerdict::Stored||stored==RequestArchiveVerdict::AlreadyStored);
        return current;
    }
};
void Equal(const CandidateRangeSchedule& left,const CandidateRangeSchedule& right){
    assert(left.chain==right.chain&&left.parent==right.parent&&left.policy==right.policy&&left.height==right.height);
    assert(left.issued==right.issued&&left.expired==right.expired&&left.hashes==right.hashes);
    assert(left.active.empty()&&right.active.empty());
}
}
int main(int argc,char** argv){
    assert(argc==2);LOCK(cs_main);Fixture fixture{fs::path{argv[1]}};
    auto* genesis=&fixture.Append(nullptr,1);auto* tip=genesis;
    for(unsigned id=2;id<=5001;++id)tip=&fixture.Append(tip,id);
    const auto configuration=ReadChainRangePolicy(fixture.view,*tip,fixture.archive,archive_budget,policy_budget);
    assert(configuration.policy);
    auto report_budget=schedule_budget;report_budget.maximum_blocks=2;
    const auto reports=ReadConfirmedNativeReports(fixture.view,*tip,fixture.archive,report_budget);
    assert(reports&&reports->empty());
    const RequestReplayBudget replay_budget{4096,262144,archive_budget,schedule_budget.scan};
    std::optional<VerifiedRequestRegistry> registry;
    const auto first_replay=ReplayConfirmedRequestHistory(fixture.view,*tip,fixture.archive,registry,replay_budget);
    assert(first_replay.verdict==RequestReplayVerdict::MoreWork&&first_replay.blocks==4096&&registry);
    assert(LookupConfirmedRequest(fixture.view,*tip,*registry,genesis->GetBlockHash()).verdict==RequestRegistryVerdict::Incomplete);
    registry.reset();
    const auto resumed_replay=ReplayConfirmedRequestHistory(fixture.view,*tip,fixture.archive,registry,replay_budget);
    assert(resumed_replay.verdict==RequestReplayVerdict::AtParent&&resumed_replay.blocks==905&&registry);
    const auto absence=LookupConfirmedRequest(fixture.view,*tip,*registry,genesis->GetBlockHash());
    assert(absence.verdict==RequestRegistryVerdict::ReadyAtParent&&!absence.location);
    registry.reset();
    const auto cached_replay=ReplayConfirmedRequestHistory(fixture.view,*tip,fixture.archive,registry,replay_budget);
    assert(cached_replay.verdict==RequestReplayVerdict::AtParent&&cached_replay.blocks==0);
    const auto baseline=ReconstructCandidateRangeSchedule(fixture.view,*tip,*configuration.policy,fixture.archive,schedule_budget);
    assert(baseline.schedule);
    auto sliced=schedule_budget;sliced.maximum_blocks=4096;
    CandidateRangeScheduleReader reader;
    const auto partial=reader.Read(fixture.view,*tip,*configuration.policy,fixture.archive,sliced);
    assert(partial.verdict==RangeScheduleVerdict::BudgetExceeded&&!partial.schedule);
    const auto complete=reader.Read(fixture.view,*tip,*configuration.policy,fixture.archive,sliced);
    assert(complete.schedule);Equal(*baseline.schedule,*complete.schedule);
    const auto hot=reader.Read(fixture.view,*tip,*configuration.policy,fixture.archive,sliced);
    assert(hot.schedule);Equal(*baseline.schedule,*hot.schedule);
    tip=&fixture.Append(tip,5002);
    const auto extension=reader.Read(fixture.view,*tip,*configuration.policy,fixture.archive,sliced);
    const auto extension_baseline=ReconstructCandidateRangeSchedule(fixture.view,*tip,*configuration.policy,fixture.archive,schedule_budget);
    assert(extension.schedule&&extension_baseline.schedule);Equal(*extension.schedule,*extension_baseline.schedule);
    auto* fork=genesis;
    for(unsigned id=20001;id<=20003;++id)fork=&fixture.Append(fork,id);
    registry.reset();
    const auto rewound_replay=ReplayConfirmedRequestHistory(fixture.view,*fork,fixture.archive,registry,replay_budget);
    assert(rewound_replay.verdict==RequestReplayVerdict::AtParent&&rewound_replay.rewound==5000&&rewound_replay.blocks==3);
    const auto branch=reader.Read(fixture.view,*fork,*configuration.policy,fixture.archive,sliced);
    const auto branch_baseline=ReconstructCandidateRangeSchedule(fixture.view,*fork,*configuration.policy,fixture.archive,schedule_budget);
    assert(branch.schedule&&branch_baseline.schedule);Equal(*branch.schedule,*branch_baseline.schedule);
    const auto branch_reports=ReadConfirmedNativeReports(fixture.view,*fork,fixture.archive,report_budget);
    assert(branch_reports&&branch_reports->empty());
    CandidateRangeScheduleReader reopened;auto tiny=sliced;tiny.maximum_blocks=2;
    const auto prefix=reopened.Read(fixture.view,*fork,*configuration.policy,fixture.archive,tiny);
    assert(!prefix.schedule&&prefix.verdict==RangeScheduleVerdict::BudgetExceeded);
    const auto reopened_complete=reopened.Read(fixture.view,*fork,*configuration.policy,fixture.archive,tiny);
    assert(reopened_complete.schedule);Equal(*reopened_complete.schedule,*branch.schedule);
    Fixture second{fs::PathFromString(std::string(argv[1])+"-complete")};
    auto* second_tip=&second.Append(nullptr,30000);
    for(unsigned id=30001;id<=30007;++id)second_tip=&second.Append(second_tip,id);
    std::optional<VerifiedRequestRegistry> complete_registry;
    auto tiny_replay=replay_budget;tiny_replay.maximum_blocks=2;
    const auto complete_replay=ReplayConfirmedRequestHistoryToParent(second.view,*second_tip,second.archive,complete_registry,tiny_replay);
    assert(complete_replay.verdict==RequestReplayVerdict::AtParent&&complete_replay.blocks==8&&complete_registry);
    return 0;
}
