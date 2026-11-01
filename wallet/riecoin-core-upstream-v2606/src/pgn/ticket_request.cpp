// MIT. Native maintained hashing/script/ancestry, no server or funded coin.
#include <pgn/ticket_request.h>
#include <chain.h>
#include <coins.h>
#include <hash.h>
namespace pgn::core {
bool TicketRequestBudgetValid(TicketRequestBudget b){
    // Bound before hashing/copying. Local implementation guard, not wire limits.
    constexpr size_t GUARD=1024*1024;
    return b.miner_bytes&&b.reward_bytes&&b.signature_bytes&&b.miner_bytes<=GUARD&&b.reward_bytes<=GUARD&&b.signature_bytes<=GUARD;
}
std::optional<uint256> TicketRequestIdentity(const TicketRequest& r,TicketRequestBudget b){
    if(!TicketRequestBudgetValid(b)||r.chain.IsNull()||r.anchor.IsNull()||r.anchor_height<0||r.miner.empty()||r.reward.empty()||
       r.miner.size()>b.miner_bytes||r.reward.size()>b.reward_bytes||
       (r.lease_seconds&&(!b.allow_time_leases||r.lease_seconds!=NativeTimedTicketLeaseSeconds)))return {};
    HashWriter h{TaggedHash(r.lease_seconds?"PGN/UNAPPROVED/TICKET-REQUEST/2":"PGN/UNAPPROVED/TICKET-REQUEST/1")};
    h<<r.chain<<r.anchor<<r.anchor_height<<r.miner<<r.reward;
    if(r.lease_seconds)h<<r.lease_seconds;
    const auto id=h.GetHash();return id.IsNull()?std::nullopt:std::optional{id};
}
TicketRequestCheck VerifyTicketRequest(const TicketRequest& r,std::span<const unsigned char> signature,TicketRequestBudget b){
    const auto id=TicketRequestIdentity(r,b);if(!id)return {TicketRequestVerdict::InvalidRequest,{}};
    if(signature.size()>b.signature_bytes)return {TicketRequestVerdict::SignatureBudgetExceeded,{}};
    const auto proof=VerifyScriptWork({r.chain,*id,r.anchor},r.miner,signature,{b.miner_bytes,b.signature_bytes});
    if(!proof.proof)return {TicketRequestVerdict::InvalidSignature,{}};
    return {TicketRequestVerdict::VerifiedStructure,VerifiedTicketRequest{r,*id}};
}
TicketAnchorVerdict InspectTicketRequestAnchor(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedTicketRequest& verified,uint32_t maximumAge){
    const auto head=view.GetPgnFactHead();if(!head)return TicketAnchorVerdict::Unenrolled;
    if(!parent.phashBlock||parent.nHeight<0||parent.GetBlockHash()!=view.GetBestBlock()||head->tip!=view.GetBestBlock())
        return TicketAnchorVerdict::StaleParent;
    const auto& r=verified.Request();if(head->chain!=r.chain)return TicketAnchorVerdict::WrongChain;
    if(r.anchor_height>parent.nHeight)return TicketAnchorVerdict::InvalidAnchor;
    if(uint64_t(parent.nHeight)-uint64_t(r.anchor_height)>maximumAge)return TicketAnchorVerdict::TooOld;
    const auto* anchor=parent.GetAncestor(r.anchor_height);
    if(!anchor||!anchor->phashBlock||anchor->GetBlockHash()!=r.anchor)return TicketAnchorVerdict::InvalidAnchor;
    return TicketAnchorVerdict::AncestorAtParent;
}
}
