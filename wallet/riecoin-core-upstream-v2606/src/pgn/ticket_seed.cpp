// MIT. No signature/science replay, search loop, device or network actuator.
#include <pgn/ticket_seed.h>
#include <pgn/request_registry.h>
#include <chain.h>
#include <hash.h>
#include <validation.h>
#include <limits>
#include <algorithm>
namespace pgn::core {
TicketSeedCheck DeriveTicketSeed(const CCoinsView& view,const CBlockIndex& parent,
    const ConfirmedTicketRequest& confirmation,uint32_t delay){
    AssertLockHeld(cs_main);
    const uint64_t height=uint64_t(confirmation.Height())+delay;
    if(!delay||height>uint64_t(std::numeric_limits<int32_t>::max()))return {TicketSeedVerdict::InvalidDelay,{}};
    if(!TicketRequestConfirmationOnBranch(view,parent,confirmation))return {TicketSeedVerdict::InvalidParent,{}};
    if(height>uint64_t(parent.nHeight))return {TicketSeedVerdict::WaitingForLaterBlock,{}};
    const auto* source=parent.GetAncestor(static_cast<int32_t>(height));
    if(!source||!source->phashBlock||!source->IsValid(BLOCK_VALID_SCRIPTS))return {TicketSeedVerdict::InvalidSeedBlock,{}};
    HashWriter hash{TaggedHash("PGN/UNAPPROVED/TICKET-SEED/1")};
    hash<<source->GetBlockHash()<<confirmation.Request().Id();
    return {TicketSeedVerdict::DerivedAtParent,ConfirmedTicketSeed{confirmation,source->GetBlockHash(),hash.GetHash(),static_cast<int32_t>(height)}};
}
bool TicketSeedOnBranch(const CCoinsView& view,const CBlockIndex& parent,const ConfirmedTicketSeed& seed){
    AssertLockHeld(cs_main);
    if(!TicketRequestConfirmationOnBranch(view,parent,seed.Confirmation())||seed.Height()>parent.nHeight)return false;
    const auto* source=parent.GetAncestor(seed.Height());
    if(!source||!source->phashBlock||!source->IsValid(BLOCK_VALID_SCRIPTS)||source->GetBlockHash()!=seed.SourceBlock())return false;
    if(!seed.HasScientificSource())return true; // Explicit OLD candidate, not upgraded.
    const auto head=view.GetPgnFactHead();const auto retained=view.GetPgnFactForward(seed.SourceBlock());
    if(!head||!source->pprev||!source->pprev->phashBlock||!retained||!retained->Valid()||!retained->before||
       retained->after!=FactHead{head->chain,seed.SourceBlock()}||
       *retained->before!=FactHead{head->chain,source->pprev->GetBlockHash()}||retained->changes.size()!=1)return false;
    const auto& change=retained->changes[0];const FactClaim claim{seed.m_work,seed.SourceBlock()};
    if(change.fact!=seed.m_fact||change.before||change.after!=std::optional{claim}||view.GetPgnFact(seed.m_fact)!=std::optional{claim})return false;
    HashWriter material{TaggedHash("PGN/UNAPPROVED/CANONICAL-SCIENCE-SOURCE/1")};material<<seed.m_policy<<seed.m_fact;
    HashWriter value{TaggedHash("PGN/UNAPPROVED/SCIENTIFIC-TICKET-SEED/1")};
    value<<head->chain<<seed.m_material<<seed.Confirmation().Request().Id();
    return material.GetHash()==seed.m_material&&value.GetHash()==seed.Value();
}
MaturedSeedCheck DeriveMaturedTicketSeeds(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedRequestRegistry& registry,const NativeRequestHistoryArchive& archive,uint32_t delay,
    RequestHistoryArchiveBudget archiveBudget,ConfirmedRequestScanBudget scanBudget,const VerifiedNativeBlockScience* scientificSource){
    AssertLockHeld(cs_main);
    if(!delay||delay>uint32_t(std::numeric_limits<int32_t>::max()))return {MaturedSeedVerdict::InvalidDelay,{}};
    const auto head=view.GetPgnFactHead();
    if(!head||!parent.phashBlock||parent.nHeight<0||!parent.IsValid(BLOCK_VALID_SCRIPTS)||
       head->tip!=parent.GetBlockHash()||head->tip!=view.GetBestBlock())return {MaturedSeedVerdict::InvalidParent,{}};
    if(registry.Chain()!=head->chain||registry.Height()!=parent.nHeight||registry.Tip()!=parent.GetBlockHash())
        return {MaturedSeedVerdict::IncompleteRegistry,{}};
    if(uint32_t(parent.nHeight)<delay)return {MaturedSeedVerdict::NoDueHeight,{}};
    const auto height=parent.nHeight-int32_t(delay);
    const auto loaded=archive.Load(view,parent,height,archiveBudget);
    if(!loaded.block)return {loaded.verdict==RequestArchiveVerdict::Missing?MaturedSeedVerdict::MissingHistory:
        loaded.verdict==RequestArchiveVerdict::BudgetExceeded?MaturedSeedVerdict::BudgetExceeded:
        loaded.verdict==RequestArchiveVerdict::InvalidParent?MaturedSeedVerdict::InvalidParent:MaturedSeedVerdict::InvalidHistory,{}};
    const auto scanned=ScanConfirmedTicketRequests(view,parent,*loaded.block,scanBudget);
    if(!scanned.batch)return {scanned.verdict==ConfirmedRequestScanVerdict::BudgetExceeded?
        MaturedSeedVerdict::BudgetExceeded:MaturedSeedVerdict::InvalidHistory,{}};
    std::vector<ConfirmedTicketSeed> seeds;seeds.reserve(scanned.batch->Requests().size());
    for(const auto& confirmed:scanned.batch->Requests()){
        const auto indexed=LookupConfirmedRequest(view,parent,registry,confirmed.Request().Id());
        if(indexed.verdict!=RequestRegistryVerdict::ReadyAtParent||!indexed.location||
           *indexed.location!=RequestLocation{confirmed.Block(),confirmed.Height(),confirmed.Output()})
            return {MaturedSeedVerdict::InvalidHistory,{}};
        auto derived=scientificSource?DeriveScientificTicketSeed(view,parent,confirmed,delay,*scientificSource):
            DeriveTicketSeed(view,parent,confirmed,delay);
        if(!derived.seed||derived.seed->Height()!=parent.nHeight)return {MaturedSeedVerdict::InvalidHistory,{}};
        seeds.push_back(std::move(*derived.seed));
    }
    std::sort(seeds.begin(),seeds.end(),[](const auto& a,const auto& b){return a.Confirmation().Request().Id()<b.Confirmation().Request().Id();});
    return {MaturedSeedVerdict::CompleteAtParent,CompleteMaturedTicketSeeds{head->chain,parent.GetBlockHash(),parent.nHeight,std::move(seeds)}};
}
bool MaturedTicketSeedsAtParent(const CCoinsView& view,const CBlockIndex& parent,const CompleteMaturedTicketSeeds& batch){
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    if(!head||batch.Height()<0||!parent.phashBlock||!parent.IsValid(BLOCK_VALID_SCRIPTS)||
       head->chain!=batch.Chain()||head->tip!=view.GetBestBlock()||head->tip!=parent.GetBlockHash()||
       batch.Parent()!=parent.GetBlockHash()||batch.Height()!=parent.nHeight)return false;
    return std::all_of(batch.Seeds().begin(),batch.Seeds().end(),[&](const auto& seed){return TicketSeedOnBranch(view,parent,seed);});
}
}
