// Public known-key synthetic native history. No live wallet/network.
#include <pgn/native_audit_replay_test_helpers.h>
#include <pgn/native_work_duty.h>
#include <pgn/native_hourly_complete_returns.h>
#include <deque>
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
// This executable alone supplies a fixed synthetic network policy. It cannot
// activate the production daemon, whose policy CPP returns nullopt.
std::optional<NativeAuditOperatorRules> pgn::core::ConfiguredNativeAuditOperatorRules(const uint256& chain){
 uint256 fixture;fixture.begin()[0]=21;
 return chain==fixture?std::optional{NativeAuditOperatorRules{fixture,7}}:std::nullopt;
}


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
    const RangeScheduleBudget schedule{64,100,100,replay.archive,scan,{64,4096,1024}};
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
    UniValue disclosure{UniValue::VOBJ},computed{UniValue::VOBJ},counts{UniValue::VARR},best{UniValue::VARR};
    for(unsigned i=0;i<100;++i)counts.push_back("0");
    disclosure.pushKV("schema","PGN_NETWORK_RANGE_REPORT_V1");disclosure.pushKV("genesis",chain.GetHex());disclosure.pushKV("request_id",lease.owner.Id().GetHex());disclosure.pushKV("ticket",HexStr(reserved.reservation->Ticket()));disclosure.pushKV("commitment_nonce",std::string(64,'b'));
    computed.pushKV("schema","PGN_HORIZON_COMPUTED_REPORT_V1");computed.pushKV("start",lease.assignment.start.convert_to<std::string>());computed.pushKV("end",lease.assignment.end.convert_to<std::string>());computed.pushKV("positions",(lease.assignment.end-lease.assignment.start).convert_to<std::string>());computed.pushKV("ticket_id",HexStr(reserved.reservation->Ticket()));computed.pushKV("counts",counts);computed.pushKV("best",best);disclosure.pushKV("computed",computed);
    const auto disclosure_text=disclosure.write();const Bytes disclosure_bytes{disclosure_text.begin(),disclosure_text.end()};
    const auto report_root=*uint256::FromHex(HexStr(AuditSha(disclosure_bytes)));
    const auto first_report=commitment_for(report_root);
    const auto report_a=make_block(parent,{CTxOut{0,*BuildNativeReportEnvelope(first_report)}},500);
    const auto report_a_hash=report_a.GetHash();CBlockIndex report_a_index{report_a};
    report_a_index.phashBlock=&report_a_hash;report_a_index.pprev=&parent;report_a_index.nHeight=4;report_a_index.nTx=1;report_a_index.nStatus=BLOCK_VALID_SCRIPTS;report_a_index.BuildSkip();
    CCoinsViewDB coins_a{{.path=path/"coins-a",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache view_a{&coins_a};view_a.SetBestBlock(report_a_hash);view_a.StagePgnFactCommit({{},FactHead{chain,report_a_hash},{}});
    need(history.StoreBlock(view_a,report_a_index,4,report_a,replay.archive)==RequestArchiveVerdict::Stored,"Native report block archive");
    auto ledger_a=compare(view_a,report_a_index,history,policy_budget,schedule,signature_budget);
    need(ledger_a&&ledger_a->size()==1,"New report did not enter incremental state");
    need(ledger_a->at(lease.owner.Id()).report.height==4&&ledger_a->at(lease.owner.Id()).owner_reveal.empty(),"First signed completion claim must precede audit disclosure");
    const auto enroll=[&](unsigned scalar){
        NativeAuditEvent event;event.action=NativeAuditAction::Enroll;event.request=ledger_a->authority_request;
        event.parent=report_a_hash;event.actor=miner;event.slot=0;event.attempt=0;
        const auto actor=GetScriptForDestination(WitnessV0KeyHash(known(scalar).GetPubKey()));
        uint256 operator_id;if(scalar==1)operator_id=NativeAuditOperator(*ledger_a,miner);else operator_id.begin()[0]=scalar;
        const auto text=std::string{"{\"schema\":\"PGN_TESTNET_PARTICIPANT_V1\",\"may_audit\":true,\"operator_id\":\""}+operator_id.GetHex()+"\",\"signing_script\":\""+HexStr(actor)+"\"}";
        event.body=Bytes{text.begin(),text.end()};const auto identity=NativeAuditWorkIdentity(chain,uint256{},event);
        event.signature=sign(*ScriptWorkChallenge({chain,identity,event.parent},miner,signature_budget),key);
        return *BuildNativeAuditEnvelope(event);
    };
    const auto admitted=make_block(report_a_index,{CTxOut{0,enroll(1)},CTxOut{0,enroll(2)},CTxOut{0,enroll(3)}},600);
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

    // Same authenticated archive, actual signed self-audit events, compared
    // against full replay at every step. Counts are synthetic agreement data.
    std::deque<CBlock> audit_blocks;std::deque<uint256> audit_hashes;std::deque<CBlockIndex> audit_indices;
    CBlockIndex* audit_tip=&admitted_index;auto active=*enrolled;
    const auto append=[&](std::vector<CTxOut> outputs){
        const int height=audit_tip->nHeight+1;audit_blocks.push_back(make_block(*audit_tip,std::move(outputs),700+height));
        audit_hashes.push_back(audit_blocks.back().GetHash());audit_indices.emplace_back(audit_blocks.back());auto& index=audit_indices.back();
        index.phashBlock=&audit_hashes.back();index.pprev=audit_tip;index.nHeight=height;index.nTx=1;index.nStatus=BLOCK_VALID_SCRIPTS;index.BuildSkip();
        CCoinsViewDB db{{.path=path/fs::PathFromString("self-coins-"+std::to_string(height)),.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
        CCoinsViewCache next{&db};next.SetBestBlock(index.GetBlockHash());next.StagePgnFactCommit({{},FactHead{chain,index.GetBlockHash()},{}});
        need(history.StoreBlock(next,index,height,audit_blocks.back(),replay.archive)==RequestArchiveVerdict::Stored,"Self event archive failed");
        auto state=compare(next,index,history,policy_budget,schedule,signature_budget);need(state.has_value(),"Self event whole replay failed");audit_tip=&index;active=*state;
    };
    const auto event=[&](NativeAuditAction action,uint8_t slot,Bytes body){
        const auto signing_key=action==NativeAuditAction::ReportReveal?key:known(2);const auto signer=GetScriptForDestination(WitnessV0KeyHash(signing_key.GetPubKey()));NativeAuditEvent e{lease.owner.Id(),audit_tip->GetBlockHash(),action,slot,0,signer,std::move(body)};
        const auto identity=NativeAuditWorkIdentity(chain,report_root,e);e.signature=sign(*ScriptWorkChallenge({chain,identity,e.parent},signer,signature_budget),signing_key);
        const auto script=BuildNativeAuditEnvelope(e);need(script.has_value(),"Self event envelope failed");return CTxOut{0,*script};
    };
    std::array<Bytes,2> answers;
    const auto partitions=NativeAuditPartitions(report_root,audit_tip->GetAncestor(5)->GetBlockHash());
    for(uint8_t slot=0;slot<2;++slot){UniValue body{UniValue::VOBJ},indices{UniValue::VARR},zeroes{UniValue::VARR};for(auto part:partitions[slot]){indices.push_back(uint64_t(part));zeroes.push_back("0");}
        body.pushKV("schema","PGN_AUDIT_ANSWER_V1");body.pushKV("ticket",HexStr(reserved.reservation->Ticket()));body.pushKV("attempt",0);body.pushKV("partitions",indices);body.pushKV("counts",zeroes);body.pushKV("nonce",std::string(64,'a'));auto text=body.write();answers[slot]=Bytes{text.begin(),text.end()};}
    need(!SelectNativeAudit(active,miner,audit_tip->nHeight,audit_tip->GetMedianTimePast()),"Self selection before activation");
    append({event(NativeAuditAction::Assign,0,NativeTimedAuditAssignmentBody()),event(NativeAuditAction::Commit,0,ParseHex(HexStr(AuditSha(answers[0]))))}); // Valid other-operator receipt before activation7.
    append({event(NativeAuditAction::Assign,1,NativeTimedAuditAssignmentBody())});
    append({event(NativeAuditAction::Commit,1,ParseHex(HexStr(AuditSha(answers[1]))))});
    append({event(NativeAuditAction::ReportReveal,0,disclosure_bytes),event(NativeAuditAction::Reveal,0,answers[0]),event(NativeAuditAction::Reveal,1,answers[1])});
    append({event(NativeAuditAction::ReportReveal,0,disclosure_bytes)}); // Duplicate authenticated return at height10.
    const auto& self=active.at(lease.owner.Id());need(NativeAuditReportQualified(active,self)&&!NativeAuditOperatorsIndependent(active,self),"Self history did not qualify honestly");
    need(std::string(NativeAuditOperatorAssurance(active,self))=="self_or_repeated_operator","False independent label");
    need(self.jobs[0]->issued==6&&self.jobs[0]->passed_height==9&&self.jobs[1]->passed_height==9,"Authenticated receipt height changed");
    auto old_branch=compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget);need(old_branch&&!old_branch->at(lease.owner.Id()).jobs[0],"Self receipt leaked backward across branch");
    CCoinsViewDB reopen_db{{.path=path/"self-restored",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};CCoinsViewCache reopen{&reopen_db};reopen.SetBestBlock(audit_tip->GetBlockHash());reopen.StagePgnFactCommit({{},FactHead{chain,audit_tip->GetBlockHash()},{}});
    auto restored_self=compare(reopen,*audit_tip,history,policy_budget,schedule,signature_budget);need(restored_self&&NativeAuditReportQualified(*restored_self,restored_self->at(lease.owner.Id())),"Restored self history differs");
    pgn::discussion::complete_return_detail::Cache receipt_cache;
    const auto read_receipts=[&](const CCoinsView& v,const CBlockIndex& tip,const NativeRequestHistoryArchive& archive){
        return pgn::discussion::ReadNativeCompleteReturns(v,tip,*ranges.policy,archive,policy_budget,schedule,signature_budget,receipt_cache);
    };
    const auto committed_only=read_receipts(view_admitted,admitted_index,history);
    need(committed_only&&committed_only->empty(),"A bare commitment was miscounted as complete return");
    const auto completed=read_receipts(reopen,*audit_tip,history);
    need(completed&&completed->size()==1,"First accepted complete body was not counted once");
    need(completed->front().first_received_utc==audit_tip->GetAncestor(8)->GetMedianTimePast(),"Receipt timestamp is not first E2 validation parent MTP");
    need(completed->front().non_expired_at_first_receipt,"Original legacy deadline filter rejected timely E2");
    const auto warm=read_receipts(reopen,*audit_tip,history);
    need(warm&&*warm==*completed,"Warm reader changed the first complete receipt");
    receipt_cache={};
    const auto cold=read_receipts(reopen,*audit_tip,history);
    need(cold&&*cold==*completed,"Cold replay changed the complete receipt");
    const auto back=read_receipts(view_admitted,admitted_index,history);
    need(back&&back->empty(),"Complete receipt leaked to an earlier canonical prefix");
    need(read_receipts(reopen,*audit_tip,history)==completed,"Restoring ancestry changed the receipt");
    const auto duty=InspectAutomaticNativeWorkDuty(*restored_self,*audit_tip,GetScriptForDestination(WitnessV0KeyHash(known(2).GetPubKey())),NativeDutySpent{},0);need(duty.checked.proof&&duty.references.size()==2,"Self history cannot serve ordinary cycle");

    // Real native index ancestry and authenticated alternate report bytes.
    const auto second_report=commitment_for(hashes[1]);
    const auto report_b=make_block(parent,{CTxOut{0,*BuildNativeReportEnvelope(second_report)}},501);
    const auto report_b_hash=report_b.GetHash();CBlockIndex report_b_index{report_b};
    report_b_index.phashBlock=&report_b_hash;report_b_index.pprev=&parent;report_b_index.nHeight=4;report_b_index.nTx=1;report_b_index.nStatus=BLOCK_VALID_SCRIPTS;report_b_index.BuildSkip();
    CCoinsViewDB coins_b{{.path=path/"coins-b",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache view_b{&coins_b};view_b.SetBestBlock(report_b_hash);view_b.StagePgnFactCommit({{},FactHead{chain,report_b_hash},{}});
    need(history.StoreBlock(view_b,report_b_index,4,report_b,replay.archive)==RequestArchiveVerdict::Stored,"Fork archive");
    auto fork=compare(view_b,report_b_index,history,policy_budget,schedule,signature_budget);
    const auto fork_receipts=read_receipts(view_b,report_b_index,history);need(fork_receipts&&fork_receipts->empty(),"Orphan receipt leaked into alternate branch");
    need(fork&&fork->participants.size()==1&&fork->begin()->second.report.commitment.root==hashes[1],"Old branch participant/report state leaked across fork");
    auto restored=compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget);
    need(restored&&restored->participants.size()==3&&restored->begin()->second.report.commitment.root==report_root,"Restored branch did not reconstruct");
    NativeRequestHistoryArchive missing{path/"missing-history",1<<20};
    need(!read_receipts(view_admitted,admitted_index,missing),"Missing history became zero activity");
    need(!compare(view_admitted,admitted_index,missing,policy_budget,schedule,signature_budget),"Different empty archive borrowed cached authority");
    need(compare(view_admitted,admitted_index,history,policy_budget,schedule,signature_budget).has_value(),"Recovery after unavailable archive");
    std::cout<<"{\"PASS\":true,\"whole_state_comparisons\":"<<comparisons<<",\"native_signed_enrollments\":3,\"native_signed_retained_audit_events\":8,\"full_incremental_retained_history\":true,\"retained_receipt_reorg_restored\":true,\"mathematics_tested\":false,\"reorg_and_restore\":true,\"warm_budget_rejections\":true,\"empty_archive_refused\":true}\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
