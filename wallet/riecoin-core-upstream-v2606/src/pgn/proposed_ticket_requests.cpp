#include <pgn/proposed_ticket_requests.h>
#include <chain.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <kernel/cs_main.h>
#include <primitives/block.h>
#include <algorithm>
#include <limits>
#include <set>
namespace pgn::core {
ProposedRequestCheck VerifyProposedTicketRequests(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedRequestRegistry& registry,const CBlock& block,ConfirmedRequestScanBudget budget,size_t maximum){
    AssertLockHeld(cs_main);const auto head=view.GetPgnFactHead();
    if(!head||!parent.phashBlock||parent.nHeight<0||parent.nHeight==std::numeric_limits<int32_t>::max()||
       !parent.IsValid(BLOCK_VALID_SCRIPTS)||head->tip!=parent.GetBlockHash()||view.GetBestBlock()!=head->tip||head->chain!=registry.Chain())
        return {ProposedRequestVerdict::InvalidParent,{},{}};
    if(registry.Height()!=parent.nHeight||registry.Tip()!=parent.GetBlockHash())return {ProposedRequestVerdict::IncompleteRegistry,{},{}};
    if(block.hashPrevBlock!=parent.GetBlockHash()||block.vtx.empty()||!block.vtx.front()||!block.vtx.front()->IsCoinBase())
        return {ProposedRequestVerdict::InvalidBlock,{},{}};
    for(size_t i=1;i<block.vtx.size();++i)if(!block.vtx[i]||block.vtx[i]->IsCoinBase())return {ProposedRequestVerdict::InvalidBlock,{},{}};
    bool mutated{};if(BlockMerkleRoot(block,&mutated)!=block.hashMerkleRoot||mutated)return {ProposedRequestVerdict::InvalidBlock,{},{}};
    const auto& outputs=block.vtx.front()->vout;
    if(!budget.maximum_outputs||!budget.maximum_requests||!budget.maximum_script_bytes||!TicketRequestBudgetValid(budget.request)||
       !maximum||registry.Size()>maximum||outputs.size()>budget.maximum_outputs)return {ProposedRequestVerdict::BudgetExceeded,{},{}};
    uint64_t remaining=budget.maximum_script_bytes;
    for(const auto& output:outputs){if(output.scriptPubKey.size()>remaining)return {ProposedRequestVerdict::BudgetExceeded,{},{}};remaining-=output.scriptPubKey.size();}
    std::set<uint256> ids;std::vector<ProposedTicketRequest> proposed;
    const auto height=parent.nHeight+1;
    for(uint32_t i=0;i<outputs.size();++i){
        const auto& output=outputs[i];const auto& script=output.scriptPubKey;
        auto cursor=script.begin();opcodetype op;std::vector<unsigned char> tag;
        if(!script.GetOp(cursor,op)||op!=OP_RETURN||!script.GetOp(cursor,op,tag)||
           tag.size()!=TicketRequestEnvelopeTag.size()||!std::equal(tag.begin(),tag.end(),TicketRequestEnvelopeTag.begin()))continue;
        if(proposed.size()==budget.maximum_requests||proposed.size()==maximum-registry.Size())
            return {ProposedRequestVerdict::BudgetExceeded,{},i};
        const auto decoded=DecodeTicketRequestEnvelope(script,budget.request);
        if(!decoded.envelope||output.nValue!=0)return {ProposedRequestVerdict::InvalidEnvelope,{},i};
        const auto& verified=decoded.envelope->Request();const auto& request=verified.Request();
        if(request.chain!=head->chain||request.anchor_height<0||request.anchor_height>parent.nHeight||
           uint64_t(height)-uint64_t(request.anchor_height)>budget.maximum_anchor_age)
            return {ProposedRequestVerdict::InvalidAnchor,{},i};
        const auto* anchor=parent.GetAncestor(request.anchor_height);
        if(!anchor||!anchor->phashBlock||!anchor->IsValid(BLOCK_VALID_SCRIPTS)||anchor->GetBlockHash()!=request.anchor)
            return {ProposedRequestVerdict::InvalidAnchor,{},i};
        if(!ids.insert(verified.Id()).second)return {ProposedRequestVerdict::DuplicateRequestId,{},i};
        const auto known=LookupConfirmedRequest(view,parent,registry,verified.Id());
        if(known.verdict!=RequestRegistryVerdict::ReadyAtParent)return {ProposedRequestVerdict::IncompleteRegistry,{},i};
        if(known.location)return {ProposedRequestVerdict::PreviouslyConfirmedRequest,{},i};
        proposed.push_back({i,verified});
    }
    return {ProposedRequestVerdict::VerifiedAtParent,
        VerifiedProposedTicketRequests{head->chain,parent.GetBlockHash(),block.GetHash(),height,block.vtx.front(),std::move(proposed)},{}};
}
}
