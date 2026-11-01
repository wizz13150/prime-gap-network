// MIT. Candidate WP signed request, NOT allocation, confirmation or admission.
#pragma once
#include <pgn/script_work_proof.h>
class CCoinsView;
class CBlockIndex;
namespace pgn::core {
inline constexpr uint32_t NativeTimedTicketLeaseSeconds=24*60*60;
// Intentionally NO chosen range, target, free nonce or asserted device identity.
// Anchor is a signed chain ancestor, not a requirement to re-sign each tip.
struct TicketRequest {
    uint256 chain,anchor;
    int32_t anchor_height;
    CScript miner,reward;
    // Zero is the original genesis-policy height lease. The timed request
    // domain explicitly signs its fixed duration; it cannot pick a range or
    // choose an arbitrary reservation lifetime.
    uint32_t lease_seconds{};
};
struct TicketRequestBudget {size_t miner_bytes,reward_bytes,signature_bytes;bool allow_time_leases{};};
bool TicketRequestBudgetValid(TicketRequestBudget);
std::optional<uint256> TicketRequestIdentity(const TicketRequest&,TicketRequestBudget);
struct TicketRequestCheck;
class VerifiedTicketRequest {
    TicketRequest m_request;uint256 m_id;
    VerifiedTicketRequest(TicketRequest request,uint256 id):m_request(std::move(request)),m_id(id){}
    friend TicketRequestCheck VerifyTicketRequest(const TicketRequest&,std::span<const unsigned char>,TicketRequestBudget);
public:
    const TicketRequest& Request()const{return m_request;}
    const uint256& Id()const{return m_id;}
};
enum class TicketRequestVerdict {VerifiedStructure,InvalidRequest,SignatureBudgetExceeded,InvalidSignature,InvalidEnvelope};
struct TicketRequestCheck {TicketRequestVerdict verdict;std::optional<VerifiedTicketRequest> request;};
// Uses maintained BIP322 directly with explicit signature budget. Opaque bounded
// reward script is committed, NOT approved payment/address/standardness policy.
TicketRequestCheck VerifyTicketRequest(const TicketRequest&,std::span<const unsigned char>,TicketRequestBudget);
enum class TicketAnchorVerdict {Unenrolled,StaleParent,WrongChain,InvalidAnchor,TooOld,AncestorAtParent};
// Caller holds native parent-state lock and passes its actual native index.
// The explicit age is a candidate policy input, NEVER an adopted default.
// AncestorAtParent is NOT request inclusion, range allocation, ticket validity,
// Sybil resistance, request ordering, first-ticket or consensus authorization.
// Native storage errors propagate; no absent/fault-to-success conversion.
TicketAnchorVerdict InspectTicketRequestAnchor(const CCoinsView&,const CBlockIndex&,
    const VerifiedTicketRequest&,uint32_t maximum_anchor_age);
}
