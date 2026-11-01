#include <pgn/request_history_archive.h>
#include <chain.h>
#include <coins.h>
#include <validation.h>
#include <streams.h>
#include <merkleblock.h>
#include <cstring>

namespace pgn::core {
namespace {
constexpr size_t CoinbaseCacheGuard=8*1024*1024,ProofCacheGuard=1024*1024;
bool BudgetValid(RequestHistoryArchiveBudget b){return b.coinbase_bytes&&b.proof_bytes&&b.coinbase_bytes<=CoinbaseCacheGuard&&b.proof_bytes<=ProofCacheGuard;}
DBParams Parameters(fs::path path,size_t bytes){
    if(path.empty()||!path.is_absolute()||!bytes)throw std::invalid_argument("Explicit absolute request-cache path and cache budget required");
    return {.path=std::move(path),.cache_bytes=bytes,.memory_only=false,.wipe_data=false,.obfuscate=false};
}
auto Key(const uint256& chain,const uint256& block){return std::pair{std::string{"PGN/request-history/v1"},std::pair{chain,block}};}
struct Record {
    uint256 chain,block;int32_t height{};
    std::vector<unsigned char> coinbase,proof;
    bool Valid()const{return !chain.IsNull()&&!block.IsNull()&&height>=0&&!coinbase.empty()&&!proof.empty()&&coinbase.size()<=CoinbaseCacheGuard&&proof.size()<=ProofCacheGuard;}
    bool operator==(const Record&)const=default;
    template<typename S>void Serialize(S& s)const{
        if(!Valid())throw std::ios_base::failure("Invalid request-history cache record");
        s<<uint8_t{1}<<chain<<block<<height<<coinbase<<proof;
    }
    template<typename S>static void BoundedRead(S& s,std::vector<unsigned char>& v,size_t maximum){
        const auto size=ReadCompactSize(s);
        if(!size||size>maximum||size>s.size())throw std::ios_base::failure("Request-history field bound");
        v.resize(size);s.read(std::as_writable_bytes(std::span{v}));
    }
    template<typename S>void Unserialize(S& s){
        Record next;uint8_t version{};s>>version;
        if(version!=1)throw std::ios_base::failure("Unsupported request-history cache version");
        s>>next.chain>>next.block>>next.height;
        BoundedRead(s,next.coinbase,CoinbaseCacheGuard);BoundedRead(s,next.proof,ProofCacheGuard);
        if(!next.Valid())throw std::ios_base::failure("Malformed request-history cache identity");
        *this=std::move(next);
    }
};
}
NativeRequestHistoryArchive::NativeRequestHistoryArchive(fs::path path,size_t bytes):m_db(Parameters(std::move(path),bytes)){}
RequestArchiveVerdict NativeRequestHistoryArchive::StoreBlock(const CCoinsView& view,const CBlockIndex& parent,
    int32_t height,const CBlock& block,RequestHistoryArchiveBudget budget){
    AssertLockHeld(cs_main);
    if(!BudgetValid(budget))return RequestArchiveVerdict::BudgetExceeded;
    // Guard coinbase allocation before constructing the native Merkle proof.
    if(block.vtx.empty()||!block.vtx[0])return RequestArchiveVerdict::InvalidProof;
    if(GetSerializeSize(TX_NO_WITNESS(*block.vtx[0]))>budget.coinbase_bytes)return RequestArchiveVerdict::BudgetExceeded;
    const auto verified=VerifyNativeRequestBlock(view,parent,height,block);
    if(verified.verdict==TicketConfirmationVerdict::InvalidParent)return RequestArchiveVerdict::InvalidParent;
    if(!verified.block)return RequestArchiveVerdict::InvalidProof;
    const CMerkleBlock compact{block,std::set<Txid>{block.vtx[0]->GetHash()}};
    if(GetSerializeSize(compact.txn)>budget.proof_bytes)return RequestArchiveVerdict::BudgetExceeded;
    DataStream encoded;encoded<<compact.txn;
    return Store(view,parent,*verified.block,std::span<const unsigned char>{
        reinterpret_cast<const unsigned char*>(encoded.data()),encoded.size()},budget);
}
RequestArchiveVerdict NativeRequestHistoryArchive::Store(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedNativeRequestBlock& block,std::span<const unsigned char> proof,RequestHistoryArchiveBudget budget){
    AssertLockHeld(cs_main);
    if(!block.Coinbase()||!block.Coinbase()->IsCoinBase())return RequestArchiveVerdict::InvalidProof;
    if(!BudgetValid(budget)||proof.size()>budget.proof_bytes||GetSerializeSize(TX_NO_WITNESS(*block.Coinbase()))>budget.coinbase_bytes)return RequestArchiveVerdict::BudgetExceeded;
    const auto verified=VerifyNativeRequestCoinbaseProof(view,parent,block.Height(),block.Coinbase(),proof,budget.proof_bytes);
    if(verified.verdict==TicketConfirmationVerdict::InvalidParent)return RequestArchiveVerdict::InvalidParent;
    if(!verified.block||verified.block->Block()!=block.Block()||verified.block->Chain()!=block.Chain())return RequestArchiveVerdict::InvalidProof;
    DataStream encoded;encoded<<TX_NO_WITNESS(*block.Coinbase());
    Record record{block.Chain(),block.Block(),block.Height(),std::vector<unsigned char>(encoded.size()),{proof.begin(),proof.end()}};
    std::memcpy(record.coinbase.data(),encoded.data(),encoded.size());
    Record old;
    if(m_db.ReadStrict(Key(record.chain,record.block),old)){
        if(old==record)return RequestArchiveVerdict::AlreadyStored;
        // Auxiliary proof padding/shape is NOT the identity of a block. Keep
        // an existing authentic record rather than adding an encoding rule.
        if(old.chain==record.chain&&old.block==record.block&&old.height==record.height&&old.coinbase==record.coinbase&&
           VerifyNativeRequestCoinbaseProof(view,parent,old.height,block.Coinbase(),old.proof,budget.proof_bytes).block)
            return RequestArchiveVerdict::AlreadyStored;
        return RequestArchiveVerdict::Conflict;
    }
    m_db.Write(Key(record.chain,record.block),record,true); // native atomic synchronous single-entry cache write
    return RequestArchiveVerdict::Stored;
}
RequestArchiveRead NativeRequestHistoryArchive::Load(const CCoinsView& view,const CBlockIndex& parent,int32_t height,RequestHistoryArchiveBudget budget)const{
    AssertLockHeld(cs_main);
    if(!BudgetValid(budget))return {RequestArchiveVerdict::BudgetExceeded,{}};
    const auto head=view.GetPgnFactHead();
    if(!head||!parent.phashBlock||parent.nHeight<0||!parent.IsValid(BLOCK_VALID_SCRIPTS)||
       head->tip!=parent.GetBlockHash()||head->tip!=view.GetBestBlock()||height<0||height>parent.nHeight)return {RequestArchiveVerdict::InvalidParent,{}};
    const auto* ancestor=parent.GetAncestor(height);
    if(!ancestor||!ancestor->phashBlock||!ancestor->IsValid(BLOCK_VALID_SCRIPTS))return {RequestArchiveVerdict::InvalidParent,{}};
    Record record;
    if(!m_db.ReadStrict(Key(head->chain,ancestor->GetBlockHash()),record))return {RequestArchiveVerdict::Missing,{}};
    if(record.chain!=head->chain||record.block!=ancestor->GetBlockHash()||record.height!=height)throw dbwrapper_error("Request-history cache key/identity mismatch");
    if(record.coinbase.size()>budget.coinbase_bytes||record.proof.size()>budget.proof_bytes)return {RequestArchiveVerdict::BudgetExceeded,{}};
    CTransactionRef coinbase;
    try{DataStream encoded{std::span<const unsigned char>{record.coinbase}};encoded>>TX_NO_WITNESS(coinbase);
        if(!encoded.empty())throw std::ios_base::failure("Trailing cached coinbase bytes");
    }catch(const std::ios_base::failure&){throw dbwrapper_error("Malformed cached native coinbase");}
    auto verified=VerifyNativeRequestCoinbaseProof(view,parent,height,coinbase,record.proof,budget.proof_bytes);
    if(!verified.block)return {RequestArchiveVerdict::InvalidProof,{}};
    return {RequestArchiveVerdict::Authenticated,std::move(verified.block)};
}
}
