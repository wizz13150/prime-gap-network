// MIT. Authenticated cache only, never an authoritative consensus database.
#pragma once
#include <pgn/ticket_request_confirmation.h>
#include <dbwrapper.h>

namespace pgn::core {
struct RequestHistoryArchiveBudget {size_t coinbase_bytes,proof_bytes;};
enum class RequestArchiveVerdict {Stored,AlreadyStored,Authenticated,Missing,InvalidParent,InvalidProof,BudgetExceeded,Conflict};
struct RequestArchiveRead {RequestArchiveVerdict verdict;std::optional<VerifiedNativeRequestBlock> block;};
class NativeRequestHistoryArchive {
    CDBWrapper m_db;
public:
    // Explicit separate path/cache budget. No wipe flag or background service.
    NativeRequestHistoryArchive(fs::path path,size_t cache_bytes);
    RequestArchiveVerdict Store(const CCoinsView&,const CBlockIndex&,const VerifiedNativeRequestBlock&,
        std::span<const unsigned char> compact_proof,RequestHistoryArchiveBudget);
    // Native accepted-block ingestion: authenticates the full transaction root
    // and builds the maintained coinbase proof itself. No caller-selected
    // request subset or externally asserted inclusion flag enters the cache.
    RequestArchiveVerdict StoreBlock(const CCoinsView&,const CBlockIndex&,int32_t height,
        const CBlock&,RequestHistoryArchiveBudget);
    // Selects the block hash from ACTUAL current native ancestry, never an
    // archive-supplied head. Reauthenticates every present record on reopen.
    // Missing is NOT empty batch or known request absence. DB errors propagate.
    RequestArchiveRead Load(const CCoinsView&,const CBlockIndex&,int32_t height,RequestHistoryArchiveBudget)const;
};
// Immutable archive retains orphaned records, while current-branch lookup never
// adopts them. Separate cache is NOT atomic CCoinsView/UTXO request-registry
// persistence or chain admission. Unsupported cache sizes need full-block path,
// not a new network cap. Non-witness coinbase body is authenticated by txid;
// request signature bytes are IN its outputs, not its transaction witness.
}
