#include <pgn/candidate_range_reader.h>
#include <pgn/native_report_commitment.h>
#include <coins.h>
#include <chain.h>
#include <kernel/cs_main.h>
#include <hash.h>
#include <limits>
#include <map>
#include <exception>
namespace pgn::core {
namespace {
// Internal historical READ view only after authenticating the actual current
// parent/policy. It cannot be supplied by a caller or fabricate a native index.
class HistoricalHead final:public CCoinsViewBacked {
    FactHead m_head;
public:
    HistoricalHead(const CCoinsView& base,uint256 chain,uint256 tip)
        :CCoinsViewBacked(const_cast<CCoinsView*>(&base)),m_head{chain,tip}{}
    uint256 GetBestBlock()const override{return m_head.tip;}
    std::optional<FactHead> GetPgnFactHead()const override{return m_head;}
};
RangeScheduleVerdict ArchiveFailure(RequestArchiveVerdict v){
    return v==RequestArchiveVerdict::Missing?RangeScheduleVerdict::MissingHistory:
        v==RequestArchiveVerdict::BudgetExceeded?RangeScheduleVerdict::BudgetExceeded:RangeScheduleVerdict::InvalidHistory;
}
uint256 ReservationIdentity(const uint256& policy,const CandidateRangeLease& lease){
    HashWriter hash{TaggedHash(lease.time_lease?"PGN/UNAPPROVED/NATIVE-RESERVATION/2":"PGN/UNAPPROVED/NATIVE-RESERVATION/1")};
    hash<<policy<<lease.assignment.request<<lease.assignment.seed<<lease.issued;
    if(lease.time_lease)hash<<lease.time_lease->issued<<lease.time_lease->deadline;
    else hash<<lease.deadline;
    return hash.GetHash();
}
}
struct CandidateRangeScheduleReader::State {
    uint256 chain,policy,tip;
    int32_t height{-1};
    SparseRangePool pool;
    std::optional<VerifiedRequestRegistry> registry;
    std::map<Integer,CandidateRangeLease> active;
    std::map<uint256,Integer> by_request;
    std::multimap<int64_t,Integer> deadlines,timeDeadlines;
    CandidateRangeSchedule result;
    explicit State(const ChainRangePolicy& source):chain(source.Chain()),policy(source.Id()),
        pool(source.Definition().origin,source.Definition().width,source.Definition().slots,
             source.Definition().selection_window,source.Definition().limit),
        result{source.Chain(),{},source.Id(),-1,{}}{}
};
CandidateRangeScheduleReader::CandidateRangeScheduleReader()=default;
CandidateRangeScheduleReader::~CandidateRangeScheduleReader()=default;
int32_t CandidateRangeScheduleReader::ProgressHeight()const{return m_state?m_state->height:-1;}
RangeScheduleCheck CandidateRangeScheduleReader::Advance(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget)
{
    AssertLockHeld(cs_main);
    m_more_work=false;
    if(!ChainRangePolicyOnBranch(view,parent,policy))return {RangeScheduleVerdict::InvalidParent,{}};
    if(!budget.maximum_blocks||!budget.maximum_requests||!budget.maximum_active_leases)
        return {RangeScheduleVerdict::BudgetExceeded,{}};
    if(m_state){
        const auto* anchor=m_state->height>=0&&m_state->height<=parent.nHeight?parent.GetAncestor(m_state->height):nullptr;
        if(m_state->chain!=policy.Chain()||m_state->policy!=policy.Id()||
           (m_state->height>=0&&(!anchor||!anchor->phashBlock||!anchor->IsValid(BLOCK_VALID_SCRIPTS)||anchor->GetBlockHash()!=m_state->tip)))
            m_state.reset();
    }
    if(!m_state)m_state=std::make_unique<State>(policy);
    struct ResetOnException {
        std::unique_ptr<State>& state;
        int depth{std::uncaught_exceptions()};
        ~ResetOnException(){if(std::uncaught_exceptions()>depth)state.reset();}
    } exception_guard{m_state};
    if(m_state->active.size()>budget.maximum_active_leases||
       (m_state->registry&&m_state->registry->Size()>budget.maximum_requests))
        return {RangeScheduleVerdict::BudgetExceeded,{}};
    const auto reject=[&](RangeScheduleVerdict verdict){m_state.reset();return RangeScheduleCheck{verdict,{}};};
    const auto& definition=policy.Definition();
    auto& pool=m_state->pool;auto& registry=m_state->registry;auto& active=m_state->active;
    auto& by_request=m_state->by_request;auto& deadlines=m_state->deadlines;
    auto& timeDeadlines=m_state->timeDeadlines;auto& result=m_state->result;
    uint64_t charged_hashes{};
    const auto expire=[&](auto& queue,int64_t now){
        while(!queue.empty()&&queue.begin()->first<=now){
            const auto lease=active.find(queue.begin()->second);
            if(lease==active.end())return false;
            // A submitted report keeps coverage occupied while audits remain
            // pending; neither clock turns that report back into free work.
            if(!lease->second.report_root){
                if(!pool.Release(queue.begin()->second))return false;
                by_request.erase(lease->second.owner.Id());active.erase(lease);++result.expired;
            }
            queue.erase(queue.begin());
        }
        return true;
    };
    const int64_t first=int64_t(m_state->height)+1;
    const int64_t last=std::min<int64_t>(parent.nHeight,first+budget.maximum_blocks-1);
    std::vector<const CBlockIndex*> ancestry;
    if(first<=last){
        ancestry.resize(size_t(last-first+1));
        auto* block=parent.GetAncestor(int32_t(last));
        for(int64_t height=last;height>=first;--height){
            if(!block||block->nHeight!=height||!block->phashBlock||!block->IsValid(BLOCK_VALID_SCRIPTS))
                return reject(RangeScheduleVerdict::InvalidHistory);
            ancestry[size_t(height-first)]=block;block=block->pprev;
        }
        if(first&&(!block||!block->phashBlock||block->GetBlockHash()!=m_state->tip))
            return reject(RangeScheduleVerdict::InvalidHistory);
    }
    for(const auto* block:ancestry){
        const auto height=size_t(block->nHeight);
        HistoricalHead current{view,policy.Chain(),block->GetBlockHash()};
        const auto loaded=archive.Load(current,*block,int32_t(height),budget.archive);
        if(!loaded.block)return reject(ArchiveFailure(loaded.verdict));
        for(const auto& output:loaded.block->Coinbase()->vout){
            if(!IsNativeReportScript(output.scriptPubKey))continue;
            const auto report=DecodeNativeReportEnvelope(output.scriptPubKey);
            if(!report||output.nValue!=0||!block->pprev||report->parent!=block->pprev->GetBlockHash())return reject(RangeScheduleVerdict::InvalidHistory);
            const auto slot=by_request.find(report->request);
            if(slot==by_request.end())return reject(RangeScheduleVerdict::InvalidHistory);
            auto& lease=active.at(slot->second);
            if(ReservationIdentity(policy.Id(),lease)!=report->ticket||(lease.report_root&&*lease.report_root!=report->root))return reject(RangeScheduleVerdict::InvalidHistory);
            lease.report_root=report->root;
        }
        if(!expire(deadlines,int64_t(height))||!expire(timeDeadlines,block->GetMedianTimePast()))return reject(RangeScheduleVerdict::InvalidHistory);
        if(height&&height-1>=definition.seed_delay){
            const auto& previous=*block->pprev;HistoricalHead previousView{view,policy.Chain(),previous.GetBlockHash()};
            auto due=DeriveMaturedTicketSeeds(previousView,previous,*registry,archive,definition.seed_delay,budget.archive,budget.scan);
            if(!due.batch)return reject(due.verdict==MaturedSeedVerdict::MissingHistory?RangeScheduleVerdict::MissingHistory:
                due.verdict==MaturedSeedVerdict::BudgetExceeded?RangeScheduleVerdict::BudgetExceeded:RangeScheduleVerdict::InvalidHistory);
            if(due.batch->Seeds().size()>budget.maximum_active_leases-active.size())return reject(RangeScheduleVerdict::BudgetExceeded);
            if(due.batch->Seeds().empty()){
                // No draw and no hash budget spent by an authenticated empty round.
            }else{
            auto roundBudget=budget.draw;roundBudget.maximum_hashes-=charged_hashes;
            if(!roundBudget.maximum_hashes)return reject(RangeScheduleVerdict::BudgetExceeded);
            auto planned=PlanCandidateRanges(previousView,previous,*due.batch,pool,roundBudget);
            if(!planned.assignments)return reject(planned.verdict==CandidateRangeVerdict::PoolExhausted?RangeScheduleVerdict::PoolExhausted:
                planned.verdict==CandidateRangeVerdict::BudgetExceeded?RangeScheduleVerdict::BudgetExceeded:RangeScheduleVerdict::InvalidHistory);
            if(planned.hashes>budget.draw.maximum_hashes-charged_hashes)return reject(RangeScheduleVerdict::BudgetExceeded);
            result.hashes+=planned.hashes;charged_hashes+=planned.hashes;
            size_t assignmentIndex{};
            for(auto& assignment:*planned.assignments){
                if(!pool.Occupy(assignment.slot))return reject(RangeScheduleVerdict::InvalidHistory);
                const Integer slot=assignment.slot;
                const auto& owner=due.batch->Seeds()[assignmentIndex++].Confirmation().Request();
                if(owner.Id()!=assignment.request)return reject(RangeScheduleVerdict::InvalidHistory);
                const auto seconds=owner.Request().lease_seconds;
                const auto deadline=seconds?int64_t{0}:int64_t(height)+definition.lease_blocks;
                CandidateRangeLease lease{std::move(assignment),owner,int32_t(height),deadline};
                if(seconds){
                    lease.time_lease=workTimeLease(block->GetMedianTimePast(),seconds);
                    if(!lease.time_lease)return reject(RangeScheduleVerdict::InvalidHistory);
                    timeDeadlines.emplace(lease.time_lease->deadline,slot);
                }else deadlines.emplace(deadline,slot);
                by_request.emplace(owner.Id(),slot);
                active.emplace(slot,std::move(lease));++result.issued;
            }
            }
        }
        auto scanned=ScanConfirmedTicketRequests(current,*block,*loaded.block,budget.scan);
        if(!scanned.batch)return reject(scanned.verdict==ConfirmedRequestScanVerdict::BudgetExceeded?RangeScheduleVerdict::BudgetExceeded:RangeScheduleVerdict::InvalidHistory);
        if(!registry){auto start=BeginConfirmedRequestRegistry(current,*block,*scanned.batch);
            if(!start.registry)return reject(RangeScheduleVerdict::InvalidHistory);
            registry=std::move(start.registry);
        }else if(const auto advance=AdvanceConfirmedRequestRegistry(current,*block,*registry,*scanned.batch,budget.maximum_requests);
            advance!=RequestRegistryVerdict::Advanced)return reject(advance==RequestRegistryVerdict::BudgetExceeded?RangeScheduleVerdict::BudgetExceeded:RangeScheduleVerdict::InvalidHistory);
        m_state->height=block->nHeight;m_state->tip=block->GetBlockHash();
        result.height=block->nHeight;result.parent=block->GetBlockHash();
    }
    if(m_state->height!=parent.nHeight||m_state->tip!=parent.GetBlockHash()){
        m_more_work=true;
        return {RangeScheduleVerdict::BudgetExceeded,{}};
    }
    // State::result contains only summary counters. Full materialization is an
    // explicit projection; a single reservation lookup must not pay for it.
    return {RangeScheduleVerdict::ReconstructedCandidate,result};
}
RangeScheduleCheck CandidateRangeScheduleReader::Read(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget){
    auto complete=Advance(view,parent,policy,archive,budget);
    if(!complete.schedule)return complete;
    complete.schedule->active.reserve(m_state->active.size());
    for(const auto& [slot,lease]:m_state->active)complete.schedule->active.push_back(lease);
    return complete;
}
RangeScheduleCheck CandidateRangeScheduleReader::Visit(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget,
    const std::function<void(const CandidateRangeLease&)>& visitor){
    auto complete=Advance(view,parent,policy,archive,budget);
    if(!complete.schedule)return complete;
    for(const auto& [slot,lease]:m_state->active)visitor(lease);
    return complete;
}
RangeLeaseCheck CandidateRangeScheduleReader::ReadLease(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget,const uint256& request){
    const auto complete=Advance(view,parent,policy,archive,budget);
    if(!complete.schedule)return {complete.verdict,{}};
    const auto slot=m_state->by_request.find(request);
    if(slot==m_state->by_request.end())return {complete.verdict,{},m_state->active.size()};
    const auto lease=m_state->active.find(slot->second);
    if(lease==m_state->active.end()||lease->second.owner.Id()!=request){
        m_state.reset();return {RangeScheduleVerdict::InvalidHistory,{}};
    }
    return {complete.verdict,lease->second,m_state->active.size()};
}
}
