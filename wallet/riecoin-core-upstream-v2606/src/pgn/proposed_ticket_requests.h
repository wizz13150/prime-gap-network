// MIT. Candidate proposed-body admission, not confirmation or adopted policy.
#pragma once
#include <pgn/request_registry.h>
class CBlock;
namespace pgn::core {
struct ProposedTicketRequest {uint32_t output;VerifiedTicketRequest request;};
enum class ProposedRequestVerdict {VerifiedAtParent,InvalidParent,IncompleteRegistry,InvalidBlock,
    BudgetExceeded,InvalidEnvelope,InvalidAnchor,DuplicateRequestId,PreviouslyConfirmedRequest};
struct ProposedRequestCheck;
class VerifiedProposedTicketRequests {
    uint256 m_chain,m_parent,m_block;int32_t m_height;CTransactionRef m_coinbase;
    std::vector<ProposedTicketRequest> m_requests;
    VerifiedProposedTicketRequests(uint256 chain,uint256 parent,uint256 block,int32_t height,CTransactionRef coinbase,
        std::vector<ProposedTicketRequest> requests)
        :m_chain(chain),m_parent(parent),m_block(block),m_height(height),m_coinbase(std::move(coinbase)),m_requests(std::move(requests)){}
    friend ProposedRequestCheck VerifyProposedTicketRequests(const CCoinsView&,const CBlockIndex&,
        const VerifiedRequestRegistry&,const CBlock&,ConfirmedRequestScanBudget,size_t);
public:
    VerifiedProposedTicketRequests(const VerifiedProposedTicketRequests&)=default;
    VerifiedProposedTicketRequests& operator=(const VerifiedProposedTicketRequests&)=default;
    const uint256& Chain()const{return m_chain;}
    const uint256& Parent()const{return m_parent;}
    const uint256& Block()const{return m_block;}
    int32_t Height()const{return m_height;}
    const CTransactionRef& Coinbase()const{return m_coinbase;}
    std::span<const ProposedTicketRequest> Requests()const{return m_requests;}
};
struct ProposedRequestCheck {
    ProposedRequestVerdict verdict;std::optional<VerifiedProposedTicketRequests> batch;std::optional<uint32_t> failed_output;
};
// cs_main held. No proposed index flags, claimed inclusion, selected subset,
// caller seed/owner/ID list or DB writes. The complete parent registry MUST be
// current. Full native Merkle root/body and every recognized request checked;
// anchor age is measured at proposed height, not current parent height.
// Resource budgets are explicit caller capacity, NOT adopted network limits.
// A returned batch is NOT confirmation, paid ticket/range or block acceptance.
ProposedRequestCheck VerifyProposedTicketRequests(const CCoinsView&,const CBlockIndex&,
    const VerifiedRequestRegistry&,const CBlock&,ConfirmedRequestScanBudget,size_t maximum_total_requests);
}
