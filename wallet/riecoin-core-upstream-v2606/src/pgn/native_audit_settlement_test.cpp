// Closed native financial-seam QA. Controlled operators and synthetic counts;
// this does not qualify independent mathematics, economic independence or P2P.
#undef NDEBUG
#include <pgn/native_audit_settlement.h>
#include <chainparams.h>
#include <key.h>
#include <script/descriptor.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <txdb.h>
#include <wallet/receive.h>
#include <wallet/sqlite.h>
#include <wallet/wallet.h>
#include <sqlite3.h>
#include <cassert>
#include <cstdio>
#include <deque>
#include <memory>
#include <numeric>
using namespace pgn::core;
const TranslateFn G_TRANSLATION_FUN{nullptr};
namespace {
uint256 Id(unsigned n){char s[65];std::snprintf(s,sizeof(s),"%064x",n);return *uint256::FromHex(s);}
struct Actor {
    CKey key{GenerateRandomKey()};
    CTxDestination destination{WitnessV0KeyHash(key.GetPubKey())};
    CScript script{GetScriptForDestination(destination)};
    Bytes Sign(const std::string& challenge)const{
        MessageVerificationResult result;const auto txs=BIP322Txs::Create(destination,challenge,result);assert(txs);
        CMutableTransaction tx{txs->m_to_sign};FillableSigningProvider keys;keys.AddKey(key);
        std::map<COutPoint,Coin> coins{{tx.vin[0].prevout,Coin{txs->m_to_spend.vout[0],1,false}}};
        std::map<int,bilingual_str> errors;assert(SignTransaction(tx,&keys,coins,{.sighash_type=SIGHASH_ALL},errors));
        DataStream bytes;bytes<<tx.vin[0].scriptWitness.stack;
        return Bytes{reinterpret_cast<const unsigned char*>(bytes.data()),reinterpret_cast<const unsigned char*>(bytes.data())+bytes.size()};
    }
};
struct MemoryWalletDB:wallet::SQLiteDatabase {
    MemoryWalletDB():SQLiteDatabase(fs::PathFromString("qa"),fs::PathFromString("qa/wallet.dat"),wallet::DatabaseOptions{},SQLITE_OPEN_MEMORY){}
};
std::unique_ptr<wallet::CWallet> QaWallet(const Actor& actor){
    auto wallet=std::make_unique<wallet::CWallet>(nullptr,"audit-settlement-qa",std::make_unique<MemoryWalletDB>());
    LOCK(wallet->cs_wallet);wallet->SetWalletFlag(wallet::WALLET_FLAG_DESCRIPTORS);
    FlatSigningProvider provider;std::string error;
    auto descriptors=Parse("wpkh("+HexStr(actor.key.GetPubKey())+")",provider,error);assert(descriptors.size()==1);
    provider.keys.emplace(actor.key.GetPubKey().GetID(),actor.key);
    wallet::WalletDescriptor desc{std::move(descriptors.front()),1,0,1,1};
    assert(wallet->AddWalletDescriptor(desc,provider,"QA only",false));return wallet;
}
CMutableTransaction Coinbase(const AuditSettlementPage& page,unsigned height){
    CMutableTransaction tx;tx.vin.resize(1);tx.vin[0].prevout.SetNull();tx.vin[0].scriptSig=CScript{}<<int64_t(height)<<OP_0;
    tx.vout=page.outputs;if(page.receipt)tx.vout.emplace_back(0,*page.receipt);return tx;
}
struct Indexes {
    std::deque<uint256> hashes;std::deque<CBlockIndex> blocks;
    Indexes(){for(unsigned i=0;i<40;++i){hashes.push_back(Id(100+i));blocks.emplace_back();auto& b=blocks.back();b.phashBlock=&hashes.back();b.nHeight=i;b.nStatus=BLOCK_VALID_SCRIPTS;if(i)b.pprev=&blocks[i-1];b.BuildSkip();}}
};
void Enroll(NativeAuditLedger& ledger,const Actor& authority,const Actor& actor,unsigned id,const CBlockIndex& parent,const uint256& chain){
    NativeAuditEvent event{ledger.authority_request,parent.GetBlockHash(),NativeAuditAction::Enroll,0,0,authority.script};
    const auto body="{\"schema\":\"PGN_TESTNET_PARTICIPANT_V1\",\"operator_id\":\""+Id(id).GetHex()+"\",\"signing_script\":\""+HexStr(actor.script)+"\",\"may_audit\":true}";
    event.body=Bytes{body.begin(),body.end()};
    event.signature=authority.Sign(*ScriptWorkChallenge({chain,NativeAuditWorkIdentity(chain,{},event),event.parent},event.actor,{4096,4096}));
    ApplyNativeAuditEvent(ledger,event,parent,chain,{4096,4096});
}
void PassedReceipt(NativeAuditLedger& ledger,const Actor& actor,const uint256& request,uint8_t slot,const CBlockIndex& parent,const uint256& chain){
    auto& report=ledger.at(request);auto& job=*report.jobs[slot];
    const auto partitions=NativeAuditPartitions(report.report.commitment.root,parent.GetAncestor(report.report.height+1)->GetBlockHash());
    std::string body="{\"schema\":\"PGN_AUDIT_ANSWER_V1\",\"ticket\":\""+HexStr(report.range.ticket_id)+"\",\"attempt\":0,\"partitions\":[";
    for(unsigned i=0;i<5;++i){if(i)body+=",";body+=std::to_string(partitions[slot][i]);}
    body+="],\"counts\":[\"0\",\"0\",\"0\",\"0\",\"0\"],\"nonce\":\""+std::string(64,'a')+"\"}";
    NativeAuditEvent event{request,parent.GetBlockHash(),NativeAuditAction::Reveal,slot,0,actor.script,Bytes{body.begin(),body.end()}};
    job.commitment=HexStr(AuditSha(event.body)); // controlled precommitted QA fixture
    event.signature=actor.Sign(*ScriptWorkChallenge({chain,NativeAuditWorkIdentity(chain,report.report.commitment.root,event),event.parent},event.actor,{4096,4096}));
    auto forged=event;forged.body.back()^=1;bool rejected{};try{ApplyNativeAuditEvent(ledger,forged,parent,chain,{4096,4096});}catch(...){rejected=true;}assert(rejected);
    ApplyNativeAuditEvent(ledger,event,parent,chain,{4096,4096});assert(job.stage==NativeAuditJob::Passed&&job.passed_height==parent.nHeight+1);
    ApplyNativeAuditEvent(ledger,event,parent,chain,{4096,4096});assert(job.passed_height==parent.nHeight+1);
}
IssuanceSnapshot Initial(const GoldEpochDefinition& d){
    const auto pot=*QuoteCumulativeAuditAccrual(d,10);
    IssuanceSnapshot s{Id(110),PGN_MAX_SUPPLY_ATOMIC,0,pot,IssuanceBudgetPartition{Id(500),PGN_MAX_SUPPLY_ATOMIC,0,pot,0,0},OrdinaryRewardPools{0,pot},GoldEpochBalances{10,10,0,0,0,0},AuditSettlementBalances{}};
    assert(s.Valid());return s;
}
void Initialize(CCoinsViewDB& db,const IssuanceSnapshot& snapshot){
    CCoinsViewCache cache{&db};cache.SetBestBlock(snapshot.tip);
    cache.StagePgnFactCommit({{},FactHead{Id(1),snapshot.tip},{}});
    cache.StagePgnIssuanceCommit({{},snapshot});cache.Flush();
}
void Publish(CCoinsViewDB& db,const IssuanceSnapshot& before,const IssuanceSnapshot& after,const CTransaction& tx,unsigned height){
    CCoinsViewCache cache{&db};AddCoins(cache,tx,height,false);cache.SetBestBlock(after.tip);
    cache.StagePgnFactHistoryStep({FactDirection::Forward,{FactHead{Id(1),before.tip},FactHead{Id(1),after.tip},{}}});
    cache.StagePgnIssuanceHistoryStep({IssuanceDirection::Forward,{before,after}});cache.Flush();
}
}
int main(int argc,char** argv){
    assert(argc==2);ECC_Context ecc;SelectParams(ChainType::TESTNET);LOCK(cs_main);
    constexpr CAmount coin=PGN_ATOMIC_UNITS_PER_COIN;
    const GoldEpochDefinition definition{200000,10,171*coin,32*coin,24*coin,8*coin,125*coin,1000*coin,40*65536};
    assert(QuoteCumulativeAuditAccrual(definition,10)==125*coin);
    Actor authority,first,second;Indexes indexes;NativeAuditLedger ledger;ledger.authority=authority.script;ledger.authority_request=Id(2);
    Enroll(ledger,authority,first,3,indexes.blocks[2],Id(1));Enroll(ledger,authority,second,4,indexes.blocks[2],Id(1));
    for(unsigned i=0;i<2;++i){
        NativeReportAudit report;report.owner=authority.script;report.report.height=1;report.report.commitment.root=Id(600+i);report.details_root=Digest{};report.expected=std::array<uint64_t,100>{};
        report.jobs[0]=NativeAuditJob{first.script,0,3,30,NativeAuditJob::Committed};
        report.jobs[1]=NativeAuditJob{second.script,0,3,30,NativeAuditJob::Committed};ledger.emplace(Id(700+i),report);
        PassedReceipt(ledger,first,Id(700+i),0,indexes.blocks[7+i],Id(1));
        PassedReceipt(ledger,second,Id(700+i),1,indexes.blocks[i?11:8],Id(1));
    }
    auto before=Initial(definition);auto after=before;after.tip=Id(900);
    const auto page=ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),after);assert(page&&page->count==2&&after.audit_settlement->active->tasks==3);
    assert(page->paid==833333333334LL&&after.pools->audit==416666666666LL);
    const auto tx=MakeTransactionRef(Coinbase(*page,11));assert(VerifyAuditSettlementOutputs(*page,*tx,{},0));
    auto bad=Coinbase(*page,11);bad.vout[0].scriptPubKey=authority.script;assert(!VerifyAuditSettlementOutputs(*page,CTransaction{bad},{},0));
    bad=Coinbase(*page,11);++bad.vout[0].nValue;assert(!VerifyAuditSettlementOutputs(*page,CTransaction{bad},{},0));
    bad=Coinbase(*page,11);bad.vout.push_back(bad.vout.back());assert(!VerifyAuditSettlementOutputs(*page,CTransaction{bad},{},0));
    auto wa=QaWallet(first),wb=QaWallet(second);const auto credit=wallet::TxGetCredit(*wa,*tx)+wallet::TxGetCredit(*wb,*tx);assert(credit==page->paid);
    const auto path=fs::PathFromString(argv[1]);
    {CCoinsViewDB db{{.path=path,.cache_bytes=1U<<20},{}};Initialize(db,before);Publish(db,before,after,*tx,11);assert(db.GetPgnIssuanceState()==after);
        for(unsigned i=0;i<page->outputs.size();++i)assert(db.GetCoin({tx->GetHash(),i})->out==page->outputs[i]);
        CCoinsViewCache child{&db};child.SetBestBlock(after.tip);child.StagePgnIssuanceHistoryStep({IssuanceDirection::Forward,{before,after}});
        assert(child.GetPgnIssuanceState()==after&&!child.GetStagedPgnIssuanceCommit()); // journal replay is deliberately idempotent
    }
    // A cold DB reopen must keep the exact next ordinal and already-created UTXO.
    {CCoinsViewDB db{{.path=path,.cache_bytes=1U<<20},{}};assert(db.GetPgnIssuanceState()==after);
        auto done=after;done.tip=Id(901);const auto page2=ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),done);
        assert(page2&&page2->first==2&&page2->count==1&&page2->paid==416666666666LL&&!done.audit_settlement->active&&done.pools->audit==0&&done.audit_settlement->total_paid==125*coin);
        assert(!VerifyAuditSettlementOutputs(*page2,*tx,{},0));const auto tx2=MakeTransactionRef(Coinbase(*page2,12));Publish(db,after,done,*tx2,12);
        CCoinsViewCache undo{&db};for(unsigned i=0;i<page2->outputs.size();++i)assert(undo.SpendCoin({tx2->GetHash(),i}));undo.SetBestBlock(after.tip);
        const auto retained=db.GetPgnIssuanceForward(done.tip);assert(retained&&retained->before==after);
        undo.StagePgnIssuanceHistoryStep({IssuanceDirection::Rewind,*retained});undo.StagePgnFactHistoryStep({FactDirection::Rewind,*db.GetPgnFactForward(done.tip)});undo.Flush();
        assert(db.GetPgnIssuanceState()==after&&!db.HaveCoin({tx2->GetHash(),0}));
        auto replay=after;replay.tip=done.tip;const auto again=ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),replay);assert(again&&again->outputs==page2->outputs&&replay==done);Publish(db,after,replay,*tx2,12);
        auto next=done;next.tip=Id(902);next.epochs->golds=20;next.epochs->last_gold_height=20;next.promised+=125*coin;next.partition->ordinary_promised+=125*coin;next.pools->audit+=125*coin;
        const auto late=ApplyAuditSettlementPage(ledger,definition,2,20,20,Id(120),next);assert(late&&late->count==1&&late->paid==125*coin&&late->outputs[0].scriptPubKey==second.script&&next.pools->audit==0);
    }
    auto tampered=after;tampered.audit_settlement->active->root=Id(999);assert(!ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),tampered));
    auto missing=before;missing.pools->audit-=1;missing.promised-=1;missing.partition->ordinary_promised-=1;assert(!ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),missing));
    auto disabled=before;disabled.audit_settlement.reset();assert(!ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),disabled));
    auto invalid_definition=definition;invalid_definition.audit_period=0;auto untouched=before;
    assert(!ApplyAuditSettlementPage(ledger,invalid_definition,2,10,10,Id(110),untouched)&&untouched==before);
    DataStream current;current<<after;IssuanceSnapshot restored;current>>restored;assert(restored==after);
    DataStream legacy;legacy<<disabled;legacy>>restored;assert(restored==disabled&&!restored.audit_settlement);
    assert(!BuildCandidateAuditSettlementPolicy(0)&&!BuildCandidateAuditSettlementPolicy(129)&&BuildCandidateAuditSettlementPolicy(2));
    std::printf("{\"PASS\":true,\"evidence_layer\":\"native signed-receipt + LevelDB UTXO/issuance + SQLite wallet-credit seam\",\"tasks_at_cutoff\":3,\"late_tasks\":1,\"pages\":2,\"paid_atomic\":\"1250000000000\",\"wallet_credit_first_page\":\"%lld\",\"cold_restart\":true,\"duplicate_rejected\":true,\"reorg_restored\":true,\"reapply_exact\":true,\"full_ConnectBlock_qualification\":false,\"independent_math_qualification\":false}\n",static_cast<long long>(credit));
}
