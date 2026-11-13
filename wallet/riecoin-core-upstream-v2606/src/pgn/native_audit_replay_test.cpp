// Public known-key synthetic native history. No live wallet/network.
#include <pgn/native_audit_replay_test_helpers.h>
#include <pgn/native_work_duty_admission.h>
#include <pgn/chain_science_policy.h>
#include <pgn/chain_monetary_policy.h>
#include <pgn/candidate_reserved_science.h>
#include <common/signmessage.h>
#include <consensus/merkle.h>
#include <key.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <streams.h>
#include <txdb.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
extern const TranslateFn G_TRANSLATION_FUN=[](const char* text){ return std::string(text); };
using namespace pgn::core;

static void need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static CKey known(unsigned scalar) {
    std::array<unsigned char,32> bytes{}; bytes.back()=scalar;
    CKey key; key.Set(bytes.begin(),bytes.end(),true); return key;
}
static Bytes sign(const std::string& message, const CKey& key) {
    auto status=MessageVerificationResult::ERR_INVALID;
    const auto pair=BIP322Txs::Create(WitnessV0KeyHash(key.GetPubKey()),message,status);
    need(pair.has_value(),"Native BIP322 challenge could not be prepared");
    CMutableTransaction tx{pair->m_to_sign}; FillableSigningProvider provider; provider.AddKey(key);
    SignatureData data;
    need(ProduceSignature(provider,MutableTransactionSignatureCreator(tx,0,pair->m_to_spend.vout[0].nValue,SignOptions{SIGHASH_ALL}),
                          pair->m_to_spend.vout[0].scriptPubKey,data),"Actual fixture key did not sign");
    UpdateInput(tx.vin[0],data);
    DataStream wire; wire<<tx.vin[0].scriptWitness.stack;
    Bytes signature(wire.size()); std::memcpy(signature.data(),wire.data(),wire.size()); return signature;
}
int main(int argc,char** argv){
  try{
    need(argc==2,"Explicit private test folder required");
    const fs::path path=fs::PathFromString(argv[1]);
    LOCK(cs_main);
    ECC_Context ecc;
    const auto key=known(1);
    const auto miner=GetScriptForDestination(WitnessV0KeyHash(key.GetPubKey()));
    const auto alternate=GetScriptForDestination(WitnessV0KeyHash(known(2).GetPubKey()));
    uint256 chain; chain.begin()[0]=21;
    const Integer lower{"150000000000000000000"};
    const EncodingLimits encoding{10002,1048576,32,1};
    const CommittedGapPolicy evidence_policy{encoding,{{128,32,32},10000},{256,256,256},{128,64},1,1};
    const RangePolicyReadBudget policy_budget{16,4096,16384,8192};
    const RangePoolDefinition pool{lower,Integer{"1000000000000000"},2,1,10};
    const auto range_script=BuildCandidateRangePolicyEnvelope(pool,policy_budget);
    const auto science_script=BuildCandidateSciencePolicyEnvelope(ScienceVerificationDefinition{1,evidence_policy,encoding},policy_budget);
    const CAmount unit=PGN_ATOMIC_UNITS_PER_COIN;
    const MonetaryPolicyDefinition money_definition{99000000*unit,1000000*unit,{200*unit,1},100,70,25,5,FeeRoutingDefinition{100,100}};
    const auto money_script=BuildCandidateMonetaryPolicyEnvelope(money_definition,policy_budget);
    need(range_script&&science_script&&money_script,"Private policy envelopes rejected");
    std::array<CBlock,4> blocks; std::array<uint256,4> hashes;
    CMutableTransaction coinbase; coinbase.vin.resize(1); coinbase.vin[0].prevout.SetNull(); coinbase.vin[0].scriptSig=CScript{}<<1<<OP_0;
    coinbase.vout={CTxOut{0,*range_script},CTxOut{0,*science_script},CTxOut{0,*money_script}};
    blocks[0].nTime=100; blocks[0].vtx={MakeTransactionRef(coinbase)}; blocks[0].hashMerkleRoot=BlockMerkleRoot(blocks[0]); hashes[0]=blocks[0].GetHash();
    const TicketRequestBudget request_budget{128,128,4096};
    TicketRequest first{chain,hashes[0],0,miner,miner}; auto second=first; second.reward=alternate;
    const auto request_signature=[&](const TicketRequest& request) {
        const auto identity=TicketRequestIdentity(request,request_budget); need(identity.has_value(),"Request identity missing");
        const auto challenge=ScriptWorkChallenge({request.chain,*identity,request.anchor},request.miner,{128,4096});
        need(challenge.has_value(),"Request challenge missing"); return sign(*challenge,key);
    };
    const auto a=BuildTicketRequestEnvelope(first,request_signature(first),request_budget);
    const auto b=BuildTicketRequestEnvelope(second,request_signature(second),request_budget);
    need(a.envelope&&b.envelope,"Native signed request envelopes rejected");
    coinbase.vout={CTxOut{0,a.envelope->Script()},CTxOut{0,b.envelope->Script()}};
    blocks[1].nTime=200; blocks[1].hashPrevBlock=hashes[0]; blocks[1].vtx={MakeTransactionRef(coinbase)};
    blocks[1].hashMerkleRoot=BlockMerkleRoot(blocks[1]); hashes[1]=blocks[1].GetHash();
    coinbase.vout={CTxOut{0,CScript{}<<OP_RETURN}};
    for (size_t height=2;height<blocks.size();++height) {
        blocks[height].nTime=300+height; blocks[height].hashPrevBlock=hashes[height-1]; blocks[height].vtx={MakeTransactionRef(coinbase)};
        blocks[height].hashMerkleRoot=BlockMerkleRoot(blocks[height]); hashes[height]=blocks[height].GetHash();
    }
    std::vector<std::unique_ptr<CBlockIndex>> indices;
    for (size_t height=0;height<blocks.size();++height) {
        auto index=std::make_unique<CBlockIndex>(blocks[height]); index->phashBlock=&hashes[height]; index->nHeight=height;
        index->nTx=1; index->nStatus=BLOCK_VALID_SCRIPTS; index->pprev=height?indices.back().get():nullptr; index->BuildSkip(); indices.push_back(std::move(index));
    }
    auto& parent=*indices.back();
    CCoinsViewDB coins{{.path=path/"coins",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache view{&coins}; view.SetBestBlock(hashes.back()); view.StagePgnFactCommit({{},FactHead{chain,hashes.back()},{}});
    NativeRequestHistoryArchive history{path/"history",1<<20};
    const ConfirmedRequestScanBudget scan{16,4,16384,1,request_budget};
    const RequestReplayBudget replay{8,16,{4096,4096},scan};
    for (size_t height=0;height<blocks.size();++height)
        need(history.StoreBlock(view,parent,height,blocks[height],replay.archive)==RequestArchiveVerdict::Stored,"Whole native history was not stored");
    const auto ranges=ReadChainRangePolicy(view,parent,history,replay.archive,policy_budget);
    const auto science=ReadChainSciencePolicy(view,parent,history,replay.archive,policy_budget);
    const auto money=ReadChainMonetaryPolicy(view,parent,history,replay.archive,policy_budget);
    need(ranges.policy&&science.policy&&money.policy,"Policies were not authenticated from fixture genesis");
    const RangeScheduleBudget schedule{8,100,100,replay.archive,scan,{64,4096,1024}};
    auto reserved=DeriveNativeCandidateReservation(view,parent,*ranges.policy,history,schedule,a.envelope->Request().Id());
    need(reserved.reservation.has_value(),"First complete native reservation missing");
    if (reserved.reservation->Lease().assignment.slot!=0)
        reserved=DeriveNativeCandidateReservation(view,parent,*ranges.policy,history,schedule,b.envelope->Request().Id());
    need(reserved.reservation&&reserved.reservation->Lease().assignment.slot==0,"Whole request set did not cover supplied source slot");

    const auto reference=[&](const CCoinsView& v,const CBlockIndex& tip,const NativeRequestHistoryArchive& archive,
                             RangePolicyReadBudget p,RangeScheduleBudget s,ScriptWorkBudget sig){
        return ReadNativeAuditLedgerWithBlockHook(v,tip,archive,p,s,sig,
            [](const NativeAuditLedger&,const CBlockIndex&,const VerifiedNativeRequestBlock&){return true;});
    };
    unsigned comparisons{};
    const auto compare=[&](const CCoinsView& v,const CBlockIndex& tip,const NativeRequestHistoryArchive& archive,
                           RangePolicyReadBudget p,RangeScheduleBudget s,ScriptWorkBudget sig){
        const auto a=ReadNativeAuditLedger(v,tip,archive,p,s,sig);const auto b=reference(v,tip,archive,p,s,sig);
        need(bool(a)==bool(b)&&(!a||NativeAuditReplaySame(*a,*b)),"Cache/reference whole state mismatch");
        ++comparisons;return a;
    };
    const ScriptWorkBudget signature_budget{128,4096};
    auto base_ledger=compare(view,parent,history,policy_budget,schedule,signature_budget);
    need(base_ledger&&base_ledger->empty()&&!base_ledger->authority.empty(),"Initial native authority state");
    const auto& lease=reserved.reservation->Lease();
    const auto commitment_for=[&](uint256 root){
        NativeReportCommitment report{lease.owner.Id(),reserved.reservation->Ticket(),root,hashes.back(),{}};
        const auto identity=NativeReportWorkIdentity(chain,report);
        report.signature=sign(*ScriptWorkChallenge({chain,identity,report.parent},miner,signature_budget),key);
        return report;
    };
    const auto make_block=[&](const CBlockIndex& previous,std::vector<CTxOut> outputs,unsigned time){
        CMutableTransaction tx;tx.vin.resize(1);tx.vin[0].prevout.SetNull();tx.vin[0].scriptSig=CScript{}<<OP_1;
        tx.vout=std::move(outputs);CBlock block;block.hashPrevBlock=previous.GetBlockHash();block.nTime=time;
        block.vtx={MakeTransactionRef(tx)};block.hashMerkleRoot=BlockMerkleRoot(block);return block;
    };
    const auto first_report=commitment_for(hashes[0]);
    const auto report_a=make_block(parent,{CTxOut{0,*BuildNativeReportEnvelope(first_report)}},500);
    const auto report_a_hash=report_a.GetHash();CBlockIndex report_a_index{report_a};
    report_a_index.phashBlock=&report_a_hash;report_a_index.pprev=&parent;report_a_index.nHeight=4;report_a_index.nTx=1;report_a_index.nStatus=BLOCK_VALID_SCRIPTS;report_a_index.BuildSkip();
    CCoinsViewDB coins_a{{.path=path/"coins-a",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache view_a{&coins_a};view_a.SetBestBlock(report_a_hash);view_a.StagePgnFactCommit({{},FactHead{chain,report_a_hash},{}});
    need(history.StoreBlock(view_a,report_a_index,4,report_a,replay.archive)==RequestArchiveVerdict::Stored,"Native report block archive");
    auto ledger_a=compare(view_a,report_a_index,history,policy_budget,schedule,signature_budget);
    need(ledger_a&&ledger_a->size()==1,"New report did not enter incremental state");
    const auto enroll=[&](unsigned scalar){
        NativeAuditEvent event;event.action=NativeAuditAction::Enroll;event.request=ledger_a->authority_request;
        event.parent=report_a_hash;event.actor=miner;event.slot=0;event.attempt=0;
        const auto actor=GetScriptForDestination(WitnessV0KeyHash(known(scalar).GetPubKey()));
        uint256 operator_id;operator_id.begin()[0]=scalar;
        const auto text=std::string{"{\"schema\":\"PGN_TESTNET_PARTICIPANT_V1\",\"may_audit\":true,\"operator_id\":\""}+operator_id.GetHex()+"\",\"signing_script\":\""+HexStr(actor)+"\"}";
        event.body=Bytes{text.begin(),text.end()};const auto identity=NativeAuditWorkIdentity(chain,uint256{},event);
        event.signature=sign(*ScriptWorkChallenge({chain,identity,event.parent},miner,signature_budget),key);
        return *BuildNativeAuditEnvelope(event);
    };
    const auto admitted=make_block(report_a_index,{CTxOut{0,enroll(2)},CTxOut{0,enroll(3)}},600);
    const auto admitted_hash=admitted.GetHash();CBlockIndex admitted_index{admitted};
    admitted_index.phashBlock=&admitted_hash;admitted_index.pprev=&report_a_index;admitted_index.nHeight=5;admitted_index.nTx=1;admitted_index.nStatus=BLOCK_VALID_SCRIPTS;admitted_index.BuildSkip();
    CCoinsViewDB coins_admitted{{.path=path/"coins-admitted",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache view_admitted{&coins_admitted};view_admitted.SetBestBlock(admitted_hash);view_admitted.StagePgnFactCommit({{},FactHead{chain,admitted_hash},{}});
    need(history.StoreBlock(view_admitted,admitted_index,5,admitted,replay.archive)==RequestArchiveVerdict::Stored,"Enrollment archive");
    auto enrolled=compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget);
    need(enrolled&&enrolled->participants.size()==3,"Incremental participant state missing");
    for(unsigned i=0;i<3;++i)need(compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget).has_value(),"Exact parent reread");
    auto too_small=signature_budget;too_small.script_bytes=1;
    need(!compare(view_admitted,admitted_index,history,policy_budget,schedule,too_small),"Warm cache bypassed signature budget");
    need(compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget).has_value(),"Native recovery after rejected budget");
    auto tiny=schedule;tiny.archive.coinbase_bytes=1;
    need(!compare(view_admitted,admitted_index,history,policy_budget,tiny,signature_budget),"Warm cache bypassed archive budget");
    tiny=schedule;tiny.maximum_blocks=0;
    need(!compare(view_admitted,admitted_index,history,policy_budget,tiny,signature_budget),"Warm cache bypassed replay budget");
    need(compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget).has_value(),"Native recovery before fork");

    // Real native index ancestry and authenticated alternate report bytes.
    const auto second_report=commitment_for(hashes[1]);
    const auto report_b=make_block(parent,{CTxOut{0,*BuildNativeReportEnvelope(second_report)}},501);
    const auto report_b_hash=report_b.GetHash();CBlockIndex report_b_index{report_b};
    report_b_index.phashBlock=&report_b_hash;report_b_index.pprev=&parent;report_b_index.nHeight=4;report_b_index.nTx=1;report_b_index.nStatus=BLOCK_VALID_SCRIPTS;report_b_index.BuildSkip();
    CCoinsViewDB coins_b{{.path=path/"coins-b",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache view_b{&coins_b};view_b.SetBestBlock(report_b_hash);view_b.StagePgnFactCommit({{},FactHead{chain,report_b_hash},{}});
    need(history.StoreBlock(view_b,report_b_index,4,report_b,replay.archive)==RequestArchiveVerdict::Stored,"Fork archive");
    auto fork=compare(view_b,report_b_index,history,policy_budget,schedule,signature_budget);
    need(fork&&fork->participants.size()==1&&fork->begin()->second.report.commitment.root==hashes[1],"Old branch participant/report state leaked across fork");
    auto restored=compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget);
    need(restored&&restored->participants.size()==3&&restored->begin()->second.report.commitment.root==hashes[0],"Restored branch did not reconstruct");
    NativeRequestHistoryArchive missing{path/"missing-history",1<<20};
    need(!compare(view_admitted,admitted_index,missing,policy_budget,schedule,signature_budget),"Different empty archive borrowed cached authority");
    need(compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget).has_value(),"Recovery after unavailable archive");

    // Force the exact resident-cache boundary using genuine signed history.
    // The callback count distinguishes incremental restoration from replay.
    using namespace native_audit_replay_detail;
    Snapshot<NoExtraState,uint8_t> spilled;
    unsigned replayed{};
    const auto disk_read=[&](){return ReadIncremental(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget,spilled,uint8_t{0},
        [&](const NativeAuditLedger&,const CBlockIndex&,const VerifiedNativeRequestBlock&,NoExtraState&){++replayed;return true;},
        [](const NoExtraState&)->std::optional<size_t>{return 0;},1);};
    auto first_disk=disk_read();need(first_disk&&spilled.disk&&!spilled.state&&replayed>0,"Full verified prefix was not checkpointed at resident boundary");
    need(NativeAuditReplaySame(first_disk->audits,*restored),"Spilled authenticated state differs from full replay");
    auto immediate=history.LoadReplayCheckpoint(0,*spilled.disk);need(immediate.has_value(),"checkpoint immediate read failed");
    DataStream immediate_stream{std::span<const unsigned char>{*immediate}};auto immediate_ledger=ReadCheckpointLedger(immediate_stream);
    need(immediate_stream.empty()&&NativeAuditReplaySame(immediate_ledger,*restored),"checkpoint immediate codec mismatch");
    replayed=0;auto second_disk=disk_read();need(second_disk&&spilled.disk&&!spilled.state&&replayed==0,"Resident boundary repeated whole history");
    need(NativeAuditReplaySame(first_disk->audits,second_disk->audits),"Checkpoint restoration changed audit state");
    auto raw=history.LoadReplayCheckpoint(0,*spilled.disk);need(raw&&raw->size()>10,"Stored checkpoint unavailable");
    need(history.StoreReplayCheckpoint(0,*raw,*spilled.disk,*spilled.disk)==0,"Unchanged checkpoint rewrote database entries");
    auto pins=*spilled.disk;
    need(history.StoreReplayCheckpoint(254,*raw,pins,{})==pins.size(),"Initial auxiliary projection missed entries");
    auto changed_bytes=*raw;size_t position=pins.front().bytes;need(pins.size()>2&&pins[1].bytes>0,"Per-report checkpoint not separated");
    changed_bytes[position]^=1;
    pins[1].digest=AuditSha(std::span<const unsigned char>{changed_bytes}.subspan(position,pins[1].bytes));
    need(history.StoreReplayCheckpoint(254,changed_bytes,pins,*spilled.disk)==1,"One changed report rewrote other entries");
    const auto changed_read=history.LoadReplayCheckpoint(254,pins);need(changed_read&&*changed_read==changed_bytes,"Atomic changed-entry projection roundtrip");
    const auto header=std::span<const unsigned char>{changed_bytes}.first(pins.front().bytes);
    const std::vector<NativeReplayCheckpointPin> header_only{pins.front()};
    need(history.StoreReplayCheckpoint(254,header,header_only,{})==pins.size(),"Fresh-context rewrite retained old branch-only cache entries");
    need(history.LoadReplayCheckpoint(254,header_only).has_value()&&!history.LoadReplayCheckpoint(254,pins),"Retired branch projection remained readable");
    raw->back()^=1;history.StoreReplayCheckpoint(0,*raw,*spilled.disk,{});
    replayed=0;auto repaired=disk_read();need(repaired&&replayed>0&&NativeAuditReplaySame(repaired->audits,*restored),"Corrupt checkpoint was accepted or prevented exact replay repair");
    Snapshot<NoExtraState,uint8_t> restarted;
    replayed=0;auto cold=ReadIncremental(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget,restarted,uint8_t{0},
        [&](const NativeAuditLedger&,const CBlockIndex&,const VerifiedNativeRequestBlock&,NoExtraState&){++replayed;return true;},
        [](const NoExtraState&)->std::optional<size_t>{return 0;},1);
    need(cold&&replayed>0&&NativeAuditReplaySame(cold->audits,*restored),"Fresh process context trusted unauthenticated disk projection");
    auto rich=*restored;auto& rich_report=rich.begin()->second;rich_report.expected.emplace();for(size_t i=0;i<100;++i)(*rich_report.expected)[i]=i*17;
    rich_report.details_root=AuditSha(Bytes{1,2,3});rich_report.owner_reveal="private-codec-fixture";
    for(unsigned slot=0;slot<2;++slot){auto& job=rich_report.jobs[slot];job.emplace();job->actor=miner;job->attempt=slot+3;job->issued=3;job->deadline=33;job->stage=slot?NativeAuditJob::Failed:NativeAuditJob::Passed;job->commitment="commitment";job->reveal="reveal";job->passed_height=17;job->time_lease=auditTimeLease(100,60);}
    DataStream encoded;WriteCheckpointLedger(encoded,rich);auto decoded=ReadCheckpointLedger(encoded);need(encoded.empty()&&NativeAuditReplaySame(rich,decoded),"Complete checkpoint codec dropped audit fields");
    NativeDutySpent spent;spent.emplace(NativeDutyReference{hashes[0],1,7,std::string(64,'a')},hashes[1]);
    DataStream spent_stream;std::vector<std::pair<uint256,size_t>> spent_offsets;ExtraCodec<NativeDutySpent>::Write(spent_stream,spent,&spent_offsets);
    auto spent_copy=ExtraCodec<NativeDutySpent>::Read(spent_stream);need(spent_stream.empty()&&spent_copy==spent&&spent_offsets.size()==1,"Spent-credit checkpoint lost or duplicated a reference");
    std::cerr<<"PASS resident-boundary, no-history same-tip, corruption repair, cold distrust, full-field codec\n";

    std::cout<<"{\"PASS\":true,\"whole_state_comparisons\":"<<comparisons<<",\"native_signed_enrollments\":2,\"reorg_and_restore\":true,\"warm_budget_rejections\":true,\"empty_archive_refused\":true}\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
