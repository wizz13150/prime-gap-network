// MIT. Maintained native chain validity/Merkle, no guessed confirmation flags.
#include <pgn/ticket_request_confirmation.h>
#include <chain.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <merkleblock.h>
#include <streams.h>
#include <validation.h>
#include <cstring>
#include <set>
#include <algorithm>
namespace pgn::core {
TicketEnvelopeCheck BuildTicketRequestEnvelope(const TicketRequest& request,std::span<const unsigned char> signature,TicketRequestBudget budget){
    auto check=VerifyTicketRequest(request,signature,budget);
    if(!check.request)return {check.verdict,{}};
    // All allocation/copy bounds already checked by the native signature gate.
    DataStream body;
    // Successful verification bounds each component to at most 1 MiB. Reserve
    // the full bounded payload, including all three CompactSize prefixes.
    body.reserve(128+request.miner.size()+request.reward.size()+signature.size());
    // Keep the framing tag stable: an old decoder rejects the unknown typed
    // body instead of ignoring it as unrelated data. Legacy bytes stay exact.
    body<<uint8_t(request.lease_seconds?2:1)<<request.chain<<request.anchor<<request.anchor_height;
    if(request.lease_seconds)body<<request.lease_seconds;
    body<<request.miner<<request.reward<<std::vector<unsigned char>(signature.begin(),signature.end());
    const std::vector<unsigned char> tag(TicketRequestEnvelopeTag.begin(),TicketRequestEnvelopeTag.end());
    std::vector<unsigned char> payload(body.size());
    if(!payload.empty())std::memcpy(payload.data(),body.data(),payload.size());
    CScript script;script<<OP_RETURN<<tag<<payload;
    return {TicketRequestVerdict::VerifiedStructure,VerifiedTicketRequestEnvelope{std::move(*check.request),std::move(script)}};
}
namespace {
bool ReadBounded(DataStream& stream,std::vector<unsigned char>& value,size_t maximum){
    const auto size=ReadCompactSize(stream);
    if(size>maximum||size>stream.size())return false;
    value.resize(size);stream.read(std::as_writable_bytes(std::span{value}));return true;
}
bool CurrentNativeParent(const CCoinsView& view,const CBlockIndex& parent,const uint256& chain){
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    return head&&parent.phashBlock&&parent.nHeight>=0&&head->chain==chain&&head->tip==view.GetBestBlock()&&
        parent.GetBlockHash()==head->tip&&parent.IsValid(BLOCK_VALID_SCRIPTS);
}
}
TicketEnvelopeCheck DecodeTicketRequestEnvelope(const CScript& script,TicketRequestBudget budget){
    const auto invalid=[](){return TicketEnvelopeCheck{TicketRequestVerdict::InvalidEnvelope,{}};};
    // The implementation guard bounds all sums to less than 4 MiB; it is NOT
    // an adopted block/consensus limit. Refuse before GetOp can allocate.
    if(!TicketRequestBudgetValid(budget)||script.size()>256+budget.miner_bytes+budget.reward_bytes+budget.signature_bytes)return invalid();
    try{
        auto cursor=script.begin();opcodetype op;std::vector<unsigned char> tag,payload;
        if(!script.GetOp(cursor,op)||op!=OP_RETURN||!script.GetOp(cursor,op,tag)||!CheckMinimalPush(tag,op)||
           tag.size()!=TicketRequestEnvelopeTag.size()||!std::equal(tag.begin(),tag.end(),TicketRequestEnvelopeTag.begin())||
           !script.GetOp(cursor,op,payload)||!CheckMinimalPush(payload,op)||cursor!=script.end())return invalid();
        DataStream stream{std::span<const unsigned char>{payload}};uint8_t version{};TicketRequest request;
        stream>>version;if(version!=1&&version!=2)return invalid();
        stream>>request.chain>>request.anchor>>request.anchor_height;
        if(version==2){
            if(!budget.allow_time_leases)return invalid();
            stream>>request.lease_seconds;
            if(request.lease_seconds!=NativeTimedTicketLeaseSeconds)return invalid();
        }
        std::vector<unsigned char> miner,reward,signature;
        if(!ReadBounded(stream,miner,budget.miner_bytes)||!ReadBounded(stream,reward,budget.reward_bytes)||
           !ReadBounded(stream,signature,budget.signature_bytes)||!stream.empty())return invalid();
        request.miner=CScript(miner.begin(),miner.end());request.reward=CScript(reward.begin(),reward.end());
        auto verified=BuildTicketRequestEnvelope(request,signature,budget);
        if(!verified.envelope)return verified;
        if(verified.envelope->Script()!=script)return invalid();
        return verified;
    }catch(const std::ios_base::failure&){return invalid();}
}
NativeRequestBlockCheck VerifyNativeRequestBlock(const CCoinsView& view,const CBlockIndex& parent,int32_t height,const CBlock& block){
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    if(!head||!CurrentNativeParent(view,parent,head->chain))return {TicketConfirmationVerdict::InvalidParent,{}};
    if(height<0||height>parent.nHeight)return {TicketConfirmationVerdict::InvalidHeight,{}};
    const auto* included=parent.GetAncestor(height);
    if(!included||!included->phashBlock||!included->IsValid(BLOCK_VALID_SCRIPTS)||!included->nTx||
       included->nTx!=block.vtx.size()||included->GetBlockHash()!=block.GetHash()||block.vtx.empty()||!block.vtx[0]||!block.vtx[0]->IsCoinBase())
        return {TicketConfirmationVerdict::UnknownOrInvalidBlock,{}};
    for(const auto& tx:block.vtx)if(!tx)return {TicketConfirmationVerdict::UnknownOrInvalidBlock,{}};
    bool mutated=false;const auto root=BlockMerkleRoot(block,&mutated);
    if(mutated||root!=included->hashMerkleRoot||root!=block.hashMerkleRoot)return {TicketConfirmationVerdict::BadMerkleRoot,{}};
    return {TicketConfirmationVerdict::IncludedAtParent,VerifiedNativeRequestBlock{head->chain,included->GetBlockHash(),height,block.vtx[0]}};
}
NativeRequestBlockCheck VerifyNativeRequestCoinbaseProof(const CCoinsView& view,const CBlockIndex& parent,int32_t height,
    const CTransactionRef& coinbase,std::span<const unsigned char> bytes,size_t maximum){
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    if(!head||!CurrentNativeParent(view,parent,head->chain))return {TicketConfirmationVerdict::InvalidParent,{}};
    if(height<0||height>parent.nHeight)return {TicketConfirmationVerdict::InvalidHeight,{}};
    const auto* included=parent.GetAncestor(height);
    if(!included||!included->phashBlock||!included->IsValid(BLOCK_VALID_SCRIPTS)||!included->nTx||!coinbase||!coinbase->IsCoinBase())
        return {TicketConfirmationVerdict::UnknownOrInvalidBlock,{}};
    const auto bad=[](){return NativeRequestBlockCheck{TicketConfirmationVerdict::BadMerkleRoot,{}};};
    // Implementation allocation guard, NOT a new network block-size rule.
    if(!maximum||maximum>1024*1024||bytes.empty()||bytes.size()>maximum)return bad();
    try{
        DataStream preflight{bytes};uint32_t count{};preflight>>count;
        if(count!=included->nTx)return bad();
        const auto hashes=ReadCompactSize(preflight);
        if(!hashes||hashes>preflight.size()/32)return bad();
        preflight.ignore(size_t(hashes)*32);
        const auto flags=ReadCompactSize(preflight);
        if(!flags||flags!=preflight.size())return bad();
        DataStream encoded{bytes};CPartialMerkleTree tree;encoded>>tree;
        if(!encoded.empty()||tree.GetNumTransactions()!=included->nTx)return bad();
        std::vector<Txid> matched;std::vector<unsigned int> positions;
        const auto root=tree.ExtractMatches(matched,positions,included->nTx);
        if(root.IsNull()||root!=included->hashMerkleRoot||matched.size()!=1||positions.size()!=1||
           positions[0]!=0||matched[0]!=coinbase->GetHash())return bad();
        return {TicketConfirmationVerdict::IncludedAtParent,VerifiedNativeRequestBlock{head->chain,included->GetBlockHash(),height,coinbase}};
    }catch(const std::ios_base::failure&){return bad();}
}
namespace {
// Shared inclusion gate, without a whole-output duplicate scan. The complete
// scanner accounts for EVERY recognized request in its semantic ID set. The
// standalone API separately pays its one whole-output uniqueness check.
TicketConfirmationVerdict CheckRequestInclusion(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedNativeRequestBlock& block,uint32_t output,const VerifiedTicketRequestEnvelope& envelope,uint32_t maximumAge){
    AssertLockHeld(cs_main);
    const auto& request=envelope.Request().Request();
    if(block.Chain()!=request.chain||!CurrentNativeParent(view,parent,request.chain))return TicketConfirmationVerdict::InvalidParent;
    const auto height=block.Height();
    if(height<=request.anchor_height||height>parent.nHeight||
       uint64_t(height)-uint64_t(request.anchor_height)>maximumAge)return TicketConfirmationVerdict::InvalidHeight;
    const auto* anchor=parent.GetAncestor(request.anchor_height);const auto* included=parent.GetAncestor(height);
    if(!anchor||!anchor->phashBlock||anchor->GetBlockHash()!=request.anchor||!included||!included->phashBlock||
       !included->IsValid(BLOCK_VALID_SCRIPTS)||included->GetBlockHash()!=block.Block())return TicketConfirmationVerdict::InvalidParent;
    const auto& outputs=block.Coinbase()->vout;
    if(output>=outputs.size()||outputs[output].nValue!=0||outputs[output].scriptPubKey!=envelope.Script())
        return TicketConfirmationVerdict::MissingPayload;
    return TicketConfirmationVerdict::IncludedAtParent;
}
}
TicketConfirmationCheck VerifyTicketRequestConfirmation(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedNativeRequestBlock& block,uint32_t output,const VerifiedTicketRequestEnvelope& envelope,uint32_t maximumAge){
    const auto verdict=CheckRequestInclusion(view,parent,block,output,envelope,maximumAge);
    if(verdict!=TicketConfirmationVerdict::IncludedAtParent)return {verdict,{}};
    const auto& outputs=block.Coinbase()->vout;
    size_t matches=0;for(const auto& out:outputs)if(out.scriptPubKey==envelope.Script())++matches;
    if(matches!=1)return {TicketConfirmationVerdict::DuplicatePayload,{}};
    return {TicketConfirmationVerdict::IncludedAtParent,ConfirmedTicketRequest{envelope.Request(),block.Block(),block.Height(),output}};
}
bool TicketRequestConfirmationOnBranch(const CCoinsView& view,const CBlockIndex& parent,const ConfirmedTicketRequest& confirmed){
    AssertLockHeld(cs_main);
    if(!CurrentNativeParent(view,parent,confirmed.Request().Request().chain)||confirmed.Height()<0||confirmed.Height()>parent.nHeight)return false;
    const auto* included=parent.GetAncestor(confirmed.Height());
    return included&&included->phashBlock&&included->IsValid(BLOCK_VALID_SCRIPTS)&&included->GetBlockHash()==confirmed.Block();
}
ConfirmedRequestScan ScanConfirmedTicketRequests(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedNativeRequestBlock& block,ConfirmedRequestScanBudget budget){
    AssertLockHeld(cs_main);
    if(!CurrentNativeParent(view,parent,block.Chain())||block.Height()<0||block.Height()>parent.nHeight)
        return {ConfirmedRequestScanVerdict::InvalidParent,{},{}};
    const auto* included=parent.GetAncestor(block.Height());
    if(!included||!included->phashBlock||!included->IsValid(BLOCK_VALID_SCRIPTS)||included->GetBlockHash()!=block.Block())
        return {ConfirmedRequestScanVerdict::InvalidParent,{},{}};
    const auto& outputs=block.Coinbase()->vout;
    if(!budget.maximum_outputs||!budget.maximum_requests||!budget.maximum_script_bytes||!TicketRequestBudgetValid(budget.request)||outputs.size()>budget.maximum_outputs)
        return {ConfirmedRequestScanVerdict::BudgetExceeded,{},{}};
    uint64_t remaining=budget.maximum_script_bytes;
    for(const auto& out:outputs){if(out.scriptPubKey.size()>remaining)return {ConfirmedRequestScanVerdict::BudgetExceeded,{},{}};remaining-=out.scriptPubKey.size();}
    std::set<uint256> ids;std::vector<ConfirmedTicketRequest> requests;
    for(uint32_t n=0;n<outputs.size();++n){
        const auto& script=outputs[n].scriptPubKey;auto cursor=script.begin();opcodetype op;std::vector<unsigned char> tag;
        if(!script.GetOp(cursor,op)||op!=OP_RETURN||!script.GetOp(cursor,op,tag)||
           tag.size()!=TicketRequestEnvelopeTag.size()||!std::equal(tag.begin(),tag.end(),TicketRequestEnvelopeTag.begin()))continue;
        if(requests.size()==budget.maximum_requests)return {ConfirmedRequestScanVerdict::BudgetExceeded,{},n};
        auto decoded=DecodeTicketRequestEnvelope(script,budget.request);
        if(!decoded.envelope)return {ConfirmedRequestScanVerdict::InvalidEnvelope,{},n};
        if(!ids.insert(decoded.envelope->Request().Id()).second)return {ConfirmedRequestScanVerdict::DuplicateRequestId,{},n};
        if(CheckRequestInclusion(view,parent,block,n,*decoded.envelope,budget.maximum_anchor_age)!=TicketConfirmationVerdict::IncludedAtParent)
            return {ConfirmedRequestScanVerdict::UnconfirmedRequest,{},n};
        requests.push_back(ConfirmedTicketRequest{decoded.envelope->Request(),block.Block(),block.Height(),n});
    }
    return {ConfirmedRequestScanVerdict::CompleteAtParent,ConfirmedRequestBatch{block.Chain(),block.Block(),block.Height(),std::move(requests)},{}};
}
bool ConfirmedRequestBatchOnBranch(const CCoinsView& view,const CBlockIndex& parent,const ConfirmedRequestBatch& batch){
    AssertLockHeld(cs_main);
    if(!CurrentNativeParent(view,parent,batch.Chain())||batch.Height()<0||batch.Height()>parent.nHeight)return false;
    const auto* included=parent.GetAncestor(batch.Height());
    return included&&included->phashBlock&&included->IsValid(BLOCK_VALID_SCRIPTS)&&included->GetBlockHash()==batch.Block();
}
}
