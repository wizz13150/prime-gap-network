// MIT. UNAPPROVED signed-request envelope and native inclusion candidate.
#pragma once
#include <pgn/ticket_request.h>
#include <primitives/transaction.h>
#include <string_view>
class CBlock;
namespace pgn::core {
inline constexpr std::string_view TicketRequestEnvelopeTag{"PGNREQ-UNAPPROVED-1"};
struct TicketEnvelopeCheck;
class VerifiedTicketRequestEnvelope {
    VerifiedTicketRequest m_request;CScript m_script;
    VerifiedTicketRequestEnvelope(VerifiedTicketRequest request,CScript script):m_request(std::move(request)),m_script(std::move(script)){}
    friend TicketEnvelopeCheck BuildTicketRequestEnvelope(const TicketRequest&,std::span<const unsigned char>,TicketRequestBudget);
public:
    const VerifiedTicketRequest& Request()const{return m_request;}
    const CScript& Script()const{return m_script;}
};
struct TicketEnvelopeCheck {TicketRequestVerdict verdict;std::optional<VerifiedTicketRequestEnvelope> envelope;};
// Full signed body, not just an unavailable hash preimage. Candidate canonical
// native serialization inside zero-valued coinbase OP_RETURN. No wire/schema,
// payment, coinbase admission or monetary policy is activated by this helper.
TicketEnvelopeCheck BuildTicketRequestEnvelope(const TicketRequest&,std::span<const unsigned char>,TicketRequestBudget);
// Decode actual untrusted block bytes with bounds BEFORE field allocation.
// Reconstruct canonical encoding and run the maintained signature verifier.
// Reject nonminimal pushes, CompactSize, trailing bytes and unknown versions.
TicketEnvelopeCheck DecodeTicketRequestEnvelope(const CScript&,TicketRequestBudget);
enum class TicketConfirmationVerdict {IncludedAtParent,InvalidParent,InvalidHeight,UnknownOrInvalidBlock,BadMerkleRoot,MissingPayload,DuplicatePayload};
struct NativeRequestBlockCheck;
// Native CTransaction has immutable body fields and a maintained cached txid.
// Retain ONLY that immutable coinbase, not a caller-mutable CBlock pointer.
// Full block Merkle work is paid ONCE; subsequent request checks recheck the
// active ancestor and exact output, not the entire block or any GPU/science.
class VerifiedNativeRequestBlock {
    uint256 m_chain,m_block;int32_t m_height;CTransactionRef m_coinbase;
    VerifiedNativeRequestBlock(uint256 chain,uint256 block,int32_t height,CTransactionRef coinbase)
        :m_chain(chain),m_block(block),m_height(height),m_coinbase(std::move(coinbase)){}
    friend NativeRequestBlockCheck VerifyNativeRequestBlock(const CCoinsView&,const CBlockIndex&,int32_t,const CBlock&);
    friend NativeRequestBlockCheck VerifyNativeRequestCoinbaseProof(const CCoinsView&,const CBlockIndex&,int32_t,
        const CTransactionRef&,std::span<const unsigned char>,size_t);
public:
    const uint256& Chain()const{return m_chain;}
    const uint256& Block()const{return m_block;}
    int32_t Height()const{return m_height;}
    const CTransactionRef& Coinbase()const{return m_coinbase;}
};
struct NativeRequestBlockCheck {TicketConfirmationVerdict verdict;std::optional<VerifiedNativeRequestBlock> block;};
NativeRequestBlockCheck VerifyNativeRequestBlock(const CCoinsView&,const CBlockIndex&,int32_t,const CBlock&);
// Compact historical cache input: maintained partial-Merkle proof authenticates
// the ENTIRE immutable coinbase at index0 against the ACTUAL SCRIPT-valid native
// block index. Bounded canonical wire preflight BEFORE native vector allocation.
// This does NOT validate a new block, authorize pruning or activate consensus.
NativeRequestBlockCheck VerifyNativeRequestCoinbaseProof(const CCoinsView&,const CBlockIndex&,int32_t,
    const CTransactionRef&,std::span<const unsigned char> proof,size_t maximum_proof_bytes);
struct TicketConfirmationCheck;
struct ConfirmedRequestScan;
struct ConfirmedRequestScanBudget;
class ConfirmedTicketRequest {
    VerifiedTicketRequest m_request;uint256 m_block;int32_t m_height;uint32_t m_output;
    ConfirmedTicketRequest(VerifiedTicketRequest request,uint256 block,int32_t height,uint32_t output)
        :m_request(std::move(request)),m_block(block),m_height(height),m_output(output){}
    friend TicketConfirmationCheck VerifyTicketRequestConfirmation(const CCoinsView&,const CBlockIndex&,
        const VerifiedNativeRequestBlock&,uint32_t,const VerifiedTicketRequestEnvelope&,uint32_t);
    friend ConfirmedRequestScan ScanConfirmedTicketRequests(const CCoinsView&,const CBlockIndex&,
        const VerifiedNativeRequestBlock&,ConfirmedRequestScanBudget);
public:
    const VerifiedTicketRequest& Request()const{return m_request;}
    const uint256& Block()const{return m_block;}
    int32_t Height()const{return m_height;}
    uint32_t Output()const{return m_output;}
};
struct TicketConfirmationCheck {TicketConfirmationVerdict verdict;std::optional<ConfirmedTicketRequest> confirmation;};
// Caller MUST hold native cs_main and provide the actual native current index.
// Requires active-branch ancestor VALID_SCRIPTS, known nTx, full native block
// transaction-Merkle root, exactly one full signed payload and zero value.
// Age is measured AT inclusion, not at the later observation tip. No caller
// bool can assert inclusion. No write, selection/PoW loop, reward or ticket.
TicketConfirmationCheck VerifyTicketRequestConfirmation(const CCoinsView&,const CBlockIndex& parent,
    const VerifiedNativeRequestBlock&,uint32_t output,const VerifiedTicketRequestEnvelope&,uint32_t maximum_anchor_age);
// Recheck a retained confirmation after tip/reorg; no science/signature redo.
// Inclusion is NOT range allocation, request uniqueness or an eligible ticket.
bool TicketRequestConfirmationOnBranch(const CCoinsView&,const CBlockIndex&,const ConfirmedTicketRequest&);

struct ConfirmedRequestScanBudget {
    uint32_t maximum_outputs,maximum_requests;
    uint64_t maximum_script_bytes;
    uint32_t maximum_anchor_age;
    TicketRequestBudget request;
};
enum class ConfirmedRequestScanVerdict {CompleteAtParent,InvalidParent,BudgetExceeded,InvalidEnvelope,UnconfirmedRequest,DuplicateRequestId};
struct ConfirmedRequestScan;
// Complete ONLY for this candidate's tagged coinbase outputs in ONE native
// block. No caller may cherry-pick a request subset. This grants no range,
// cross-block uniqueness, historical registry, bootstrap or block admission.
class ConfirmedRequestBatch {
    uint256 m_chain,m_block;int32_t m_height;
    std::vector<ConfirmedTicketRequest> m_requests;
    ConfirmedRequestBatch(uint256 chain,uint256 block,int32_t height,std::vector<ConfirmedTicketRequest> requests)
        :m_chain(chain),m_block(block),m_height(height),m_requests(std::move(requests)){}
    friend ConfirmedRequestScan ScanConfirmedTicketRequests(const CCoinsView&,const CBlockIndex&,
        const VerifiedNativeRequestBlock&,ConfirmedRequestScanBudget);
public:
    const uint256& Chain()const{return m_chain;}
    const uint256& Block()const{return m_block;}
    int32_t Height()const{return m_height;}
    std::span<const ConfirmedTicketRequest> Requests()const{return m_requests;}
};
struct ConfirmedRequestScan {
    ConfirmedRequestScanVerdict verdict;
    std::optional<ConfirmedRequestBatch> batch;
    std::optional<uint32_t> failed_output;
};
// Native cs_main held. Bounds the entire scan BEFORE any signature work.
// Decodes and verifies EVERY matching tag, rejects any malformed/duplicate
// request, publishes no successful subset on failure. Other data ignored.
ConfirmedRequestScan ScanConfirmedTicketRequests(const CCoinsView&,const CBlockIndex&,
    const VerifiedNativeRequestBlock&,ConfirmedRequestScanBudget);
bool ConfirmedRequestBatchOnBranch(const CCoinsView&,const CBlockIndex&,const ConfirmedRequestBatch&);
}
