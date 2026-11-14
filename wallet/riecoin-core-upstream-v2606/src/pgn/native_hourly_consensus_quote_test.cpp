#include <pgn/native_hourly_loaded_targets.h>
#include <pgn/native_network_metrics.h>
// Public known-key synthetic native history. No live wallet/network.
#include <pgn/native_audit_replay_test_helpers.h>
#include <pgn/native_work_duty.h>
#include <pgn/native_hourly_consensus_quote.h>
#include <pgn/native_hourly_activation.h>
#include <pgn/native_science_target.h>
#include <pgn/native_retarget_policy.h>
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
static uint256 hourly_fixture_genesis;
// Only this executable selects a future hourly rule for its synthetic genesis.
// The production activation CPP deliberately returns nullopt.
std::optional<NativeHourlyMeritActivation> pgn::core::ConfiguredNativeHourlyMerit(const uint256& genesis){
    if(genesis!=hourly_fixture_genesis||genesis.IsNull())return {};
    return NativeHourlyMeritActivation{genesis,10800};
}
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
    const RangePoolDefinition pool{lower,Integer{"200000000000000"},2,1,10};
    const auto range_script=BuildCandidateRangePolicyEnvelope(pool,policy_budget);
    const auto science_script=BuildCandidateSciencePolicyEnvelope(ScienceVerificationDefinition{1,evidence_policy,encoding},policy_budget);
    const auto target_script=BuildCandidateNativeRetargetEnvelope({65536,65536,64U*65536U,600,86400},policy_budget);
    const CAmount unit=PGN_ATOMIC_UNITS_PER_COIN;
    const MonetaryPolicyDefinition money_definition{99000000*unit,1000000*unit,{200*unit,1},100,70,25,5,FeeRoutingDefinition{100,100}};
    const auto money_script=BuildCandidateMonetaryPolicyEnvelope(money_definition,policy_budget);
    need(range_script&&science_script&&money_script&&target_script,"Private policy envelopes rejected");
    std::array<CBlock,4> blocks; std::array<uint256,4> hashes;
    CMutableTransaction coinbase; coinbase.vin.resize(1); coinbase.vin[0].prevout.SetNull(); coinbase.vin[0].scriptSig=CScript{}<<1<<OP_0;
    coinbase.vout={CTxOut{0,*range_script},CTxOut{0,*science_script},CTxOut{0,*money_script},CTxOut{0,*target_script}};
    blocks[0].nTime=7300; blocks[0].vtx={MakeTransactionRef(coinbase)}; blocks[0].hashMerkleRoot=BlockMerkleRoot(blocks[0]); hashes[0]=blocks[0].GetHash();
    blocks[0].nNonce=2;blocks[0].nBits=0x207fffff;hashes[0]=blocks[0].GetHash();hourly_fixture_genesis=hashes[0];
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
    blocks[1].nTime=7400;blocks[1].nNonce=2;blocks[1].nBits=0x207fffff; blocks[1].hashPrevBlock=hashes[0]; blocks[1].vtx={MakeTransactionRef(coinbase)};
    blocks[1].hashMerkleRoot=BlockMerkleRoot(blocks[1]); hashes[1]=blocks[1].GetHash();
    coinbase.vout={CTxOut{0,CScript{}<<OP_RETURN}};
    for (size_t height=2;height<blocks.size();++height) {
        blocks[height].nTime=7500+height;blocks[height].nNonce=2;blocks[height].nBits=0x207fffff; blocks[height].hashPrevBlock=hashes[height-1]; blocks[height].vtx={MakeTransactionRef(coinbase)};
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
    need(NativeScientificHeaderTarget(*science.policy)==std::optional<uint32_t>{65536},"Legacy native header encoding changed");
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
        tx.vout=std::move(outputs);CBlock block;block.hashPrevBlock=previous.GetBlockHash();block.nTime=time+7200;block.nNonce=2;block.nBits=0x207fffff;
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
    pgn::discussion::complete_return_detail::Cache quote_cache;
    std::optional<pgn::discussion::HourlyConsensusResult> future_quote;
    const auto append=[&](std::vector<CTxOut> outputs,unsigned custom_time=0){
        const int height=audit_tip->nHeight+1;audit_blocks.push_back(make_block(*audit_tip,std::move(outputs),custom_time?custom_time:700+height));
        audit_hashes.push_back(audit_blocks.back().GetHash());audit_indices.emplace_back(audit_blocks.back());auto& index=audit_indices.back();
        index.phashBlock=&audit_hashes.back();index.pprev=audit_tip;index.nHeight=height;index.nTx=1;index.nStatus=BLOCK_VALID_SCRIPTS;index.BuildSkip();
        CCoinsViewDB db{{.path=path/fs::PathFromString("self-coins-"+std::to_string(height)),.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
        CCoinsViewCache next{&db};next.SetBestBlock(index.GetBlockHash());next.StagePgnFactCommit({{},FactHead{chain,index.GetBlockHash()},{}});
        need(history.StoreBlock(next,index,height,audit_blocks.back(),replay.archive)==RequestArchiveVerdict::Stored,"Self event archive failed");
        auto state=compare(next,index,history,policy_budget,schedule,signature_budget);need(state.has_value(),"Self event whole replay failed");audit_tip=&index;active=*state;
        if(custom_time){future_quote=pgn::discussion::ReadNativeHourlyConsensusQuote(next,index,*ranges.policy,history,policy_budget,schedule,signature_budget,quote_cache,UINT32_MAX);need(future_quote->verdict==pgn::discussion::HourlyConsensusVerdict::Ready&&future_quote->quote.has_value(),"Native future-window quote unavailable");}
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
    const auto canonical_quote=pgn::discussion::ReadNativeHourlyConsensusQuote(reopen,*audit_tip,*ranges.policy,history,policy_budget,schedule,signature_budget,quote_cache,UINT32_MAX);
    need(canonical_quote.verdict==pgn::discussion::HourlyConsensusVerdict::Ready&&canonical_quote.quote.has_value(),"Native quote missing on authenticated view");
    need(canonical_quote.quote->snapshot.hour==audit_tip->GetMedianTimePast()/3600*3600,"Quote clock is not native parent MTP");
    need(canonical_quote.quote->snapshot.activation_anchor==pgn::discussion::complete_return_detail::Id(audit_tip->GetBlockHash()),"Quote detached from native parent");
    NativeRequestHistoryArchive quote_missing{path/"quote-missing-history",1<<20};
    const auto unavailable_quote=pgn::discussion::ReadNativeHourlyConsensusQuote(reopen,*audit_tip,*ranges.policy,quote_missing,policy_budget,schedule,signature_budget,quote_cache,UINT32_MAX);
    need(unavailable_quote.verdict==pgn::discussion::HourlyConsensusVerdict::AwaitHistory&&!unavailable_quote.quote,"Missing archive authorized a fabricated zero/previous target");
    const auto recovered_quote=pgn::discussion::ReadNativeHourlyConsensusQuote(reopen,*audit_tip,*ranges.policy,history,policy_budget,schedule,signature_budget,quote_cache,UINT32_MAX);
    need(recovered_quote.quote&&recovered_quote.quote->snapshot==canonical_quote.quote->snapshot,"Restoring native history changed the quote");
    for(unsigned i=0;i<12;++i)append({},4000+i);
    need(future_quote&&future_quote->quote&&future_quote->quote->snapshot.hour==10800,"Canonical clock did not enter next hour");
    uint64_t projected_returns=0;for(auto count:future_quote->quote->snapshot.counts)projected_returns+=count;
    need(projected_returns==1,"Authenticated complete return was not counted exactly once in a future hourly window");
    CCoinsViewDB future_db{{.path=path/"hourly-future",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache future_view{&future_db};future_view.SetBestBlock(audit_tip->GetBlockHash());
    future_view.StagePgnFactCommit({{},FactHead{chain,audit_tip->GetBlockHash()},{}});
    const auto future_science=ReadChainSciencePolicy(future_view,*audit_tip,history,replay.archive,policy_budget,&schedule,&signature_budget);
    need(future_science.policy.has_value(),"Hourly target did not enter the actual native science-policy reader");
    // Regression: the actual work/signature preimage, not just numeric equality.
    ScientificFactContext hourly_identity{};hourly_identity.chain=chain;
    hourly_identity.range.start=1000;hourly_identity.range.length=2000;
    hourly_identity.range.segments=1;hourly_identity.range.ticket_id[0]=1;
    hourly_identity.report_root[0]=2;
    hourly_identity.policy=future_science.policy->Definition().evidence;
    hourly_identity.proof_encoding=future_science.policy->Definition().proof;
    const auto floor_identity=pgn::discussion::QuoteHourlyTarget(275,UINT32_MAX);
    need(floor_identity.has_value(),"Floor identity quote unavailable");
    hourly_identity.policy.target_numerator=floor_identity->proof_numerator;
    hourly_identity.policy.target_denominator=floor_identity->proof_denominator;
    auto legacy_identity=hourly_identity;
    legacy_identity.policy.target_numerator=55;legacy_identity.policy.target_denominator=2;
    const auto legacy_work=ScientificWorkIdentity(legacy_identity);
    need(legacy_work&&ScientificWorkIdentity(hourly_identity)==legacy_work,
         "Same27.5 changed the real native scientific signature challenge");
    auto unreduced_identity=legacy_identity;
    unreduced_identity.policy.target_numerator=275;unreduced_identity.policy.target_denominator=10;
    need(ScientificWorkIdentity(unreduced_identity)!=legacy_work,
         "Historical identity encoding was silently weakened to hide this regression");
    auto changed_identity=legacy_identity;
    changed_identity.policy.target_numerator=28;changed_identity.policy.target_denominator=1;
    need(ScientificWorkIdentity(changed_identity)!=legacy_work,
         "A genuinely different target reused the old signature challenge");

    need(future_science.policy->Definition().evidence.target_numerator==future_quote->quote->target.proof_numerator&&
         future_science.policy->Definition().evidence.target_denominator==future_quote->quote->target.proof_denominator,
         "Native proof threshold differs from the canonical hourly quote");
    need(NativeScientificHeaderTarget(*future_science.policy)==std::optional<uint32_t>{future_quote->quote->target.header_q16},
         "Native template/header identifier differs from the proof threshold");
    need(ChainSciencePolicyOnBranch(future_view,*audit_tip,*future_science.policy)&&
         !ChainSciencePolicyOnBranch(view,parent,*future_science.policy),"Hourly policy escaped its exact native parent");
    need(!ReadChainSciencePolicy(future_view,*audit_tip,history,replay.archive,policy_budget).policy,
         "An activated consumer without receipt context silently used the old target");
    need(!ReadChainSciencePolicy(future_view,*audit_tip,quote_missing,replay.archive,policy_budget,&schedule,&signature_budget).policy,
         "An empty local archive silently authorized a zero/legacy target");
    const auto old_science=ReadChainSciencePolicy(view,parent,history,replay.archive,policy_budget);
    need(old_science.policy&&NativeScientificHeaderTarget(*old_science.policy)==std::optional<uint32_t>{65536},
         "A later hourly activation changed the historical target");
    auto exact=evidence_policy;
    for(unsigned tenths=275;tenths<=640;++tenths){
        exact.target_numerator=tenths;exact.target_denominator=10;
        const auto decimal=pgn::discussion::QuoteHourlyTarget(tenths,64U*65536U);
        need(decimal&&EncodeNativeScienceTarget(exact)==std::optional<uint32_t>{decimal->header_q16},
             "Exact decimal proof/header encoding diverged");
        need(exact.target_numerator==tenths&&exact.target_denominator==10,"Encoding rewrote the scientific proof threshold");
    }
    for(unsigned q=65536;q<64U*65536U;q+=12345){
        exact.target_numerator=q;exact.target_denominator=65536;
        need(EncodeNativeScienceTarget(exact)==std::optional<uint32_t>{q},"Historical Q16 threshold changed");
    }
    exact.target_numerator=641;exact.target_denominator=10;need(!EncodeNativeScienceTarget(exact),"Oversized header target accepted");
    exact.target_numerator=0;need(!EncodeNativeScienceTarget(exact),"Zero scientific target accepted");
    exact.target_numerator=281;exact.target_denominator=0;need(!EncodeNativeScienceTarget(exact),"Zero target denominator accepted");
    const auto* historical_parent=audit_tip;
    const auto historical_quote=future_quote->quote;
    for(unsigned i=0;i<12;++i)append({},11200+i);
    CCoinsViewDB latest_db{{.path=path/"hourly-latest",.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},{}};
    CCoinsViewCache latest_view{&latest_db};latest_view.SetBestBlock(audit_tip->GetBlockHash());
    latest_view.StagePgnFactCommit({{},FactHead{chain,audit_tip->GetBlockHash()},{}});
    need(audit_tip->GetMedianTimePast()/3600*3600==18000,"Historical-pruning fixture clock changed");
    const auto recent_after_prune=read_receipts(latest_view,*audit_tip,history);
    need(recent_after_prune&&recent_after_prune->empty(),"Old detailed receipt did not leave the recent window");
    auto historical=pgn::discussion::ReadNativeHourlyHistory(latest_view,*audit_tip,*ranges.policy,history,policy_budget,schedule,signature_budget,quote_cache);
    need(historical.history.has_value(),"Authenticated historical counter projection missing");
    const auto old_from_later=historical.history->QuoteAt(historical_parent->nHeight,historical_parent->GetBlockHash());
    need(old_from_later.quote&&old_from_later.quote->snapshot.counts==historical_quote->snapshot.counts&&
         old_from_later.quote->target.header_q16==historical_quote->target.header_q16,
         "Pruning recent rows changed an earlier authenticated hourly quote");
    const auto latest_quote=historical.history->QuoteAt(audit_tip->nHeight,audit_tip->GetBlockHash());
    uint64_t latest_count=0;need(latest_quote.quote.has_value(),"Latest hourly quote missing");
    for(auto count:latest_quote.quote->snapshot.counts)latest_count+=count;
    need(latest_count==0,"Old receipts leaked into a later empty hourly window");
    quote_cache={};
    const auto cold_historical=pgn::discussion::ReadNativeHourlyHistory(latest_view,*audit_tip,*ranges.policy,history,policy_budget,schedule,signature_budget,quote_cache);
    need(cold_historical.history.has_value(),"Cold historical counter replay missing");
    const auto cold_old=cold_historical.history->QuoteAt(historical_parent->nHeight,historical_parent->GetBlockHash());
    need(cold_old.quote&&cold_old.quote->snapshot==old_from_later.quote->snapshot,"Cold replay changed a historical hourly snapshot");
    need(!pgn::discussion::ReadNativeHourlyHistory(latest_view,*audit_tip,*ranges.policy,quote_missing,policy_budget,schedule,signature_budget,quote_cache).history,
         "Missing archive borrowed an old sealed historical projection");
    // Difficulty presentation is a task13 consumer, not an audit-policy test.
    const std::optional<MeritControllerDefinition> metrics_policy{MeritControllerDefinition{65536,65536,64U*65536U,600,86400}};
    const auto old_metric=NativeNetworkTargetAt(parent,*metrics_policy);
    need(old_metric&&!old_metric->hourly&&old_metric->header_q16==65536,"Metrics rewrote a historical pre-activation target");
    const auto hourly_metric=NativeNetworkTargetAt(*historical_parent,*metrics_policy,&*cold_historical.history);
    need(hourly_metric&&hourly_metric->hourly&&hourly_metric->header_q16==historical_quote->target.header_q16&&
         hourly_metric->numerator==historical_quote->target.proof_numerator&&
         hourly_metric->denominator==historical_quote->target.proof_denominator,"Metrics differ from the native proof threshold");
    need(!NativeNetworkTargetAt(*historical_parent,*metrics_policy),"Missing hourly metrics history silently fell back to the legacy controller");
    CBlockIndex foreign_parent{historical_parent->GetBlockHeader()};uint256 foreign_hash=uint256::ONE;foreign_parent.phashBlock=&foreign_hash;
    foreign_parent.pprev=historical_parent->pprev;foreign_parent.nHeight=historical_parent->nHeight;foreign_parent.BuildSkip();
    need(!NativeNetworkTargetAt(foreign_parent,*metrics_policy,&*cold_historical.history),"Metrics accepted an unrelated branch");
    CBlockHeader hourly_header;hourly_header.hashPrevBlock=historical_parent->GetBlockHash();
    hourly_header.nTime=historical_parent->nTime+1;hourly_header.nNonce=0;hourly_header.nBits=historical_quote->target.header_q16;
    CBlockIndex loaded_hourly{hourly_header};const auto loaded_hash=hourly_header.GetHash();
    loaded_hourly.phashBlock=&loaded_hash;loaded_hourly.pprev=const_cast<CBlockIndex*>(historical_parent);
    loaded_hourly.nHeight=historical_parent->nHeight+1;loaded_hourly.BuildSkip();
    need(NativeLoadedHourlyHeaderMatches(loaded_hourly,*cold_historical.history),"Cold-loaded hourly header disagrees with the authenticated historical quote");
    ++loaded_hourly.nBits;
    need(!NativeLoadedHourlyHeaderMatches(loaded_hourly,*cold_historical.history),"Cold-load gate accepted a one-unit target mutation");
    --loaded_hourly.nBits;loaded_hourly.nHeight+=1;
    need(!NativeLoadedHourlyHeaderMatches(loaded_hourly,*cold_historical.history),"Cold-load gate accepted an invalid parent height");
    --loaded_hourly.nHeight;loaded_hourly.nNonce=2;
    need(!NativeLoadedHourlyHeaderMatches(loaded_hourly,*cold_historical.history),"A bootstrap record was misrepresented as a scientific header");
    const auto metrics=NativeNetworkMetrics(*restored_self,*audit_tip,*metrics_policy,16,3600,600,nullptr,&*cold_historical.history);
    need(metrics["points"].size()>1,"Historical metrics samples missing");
    for(const auto& point:metrics["points"].getValues()){
        const auto* selected=audit_tip->GetAncestor(point["height"].getInt<int>());
        need(selected,"Metrics selected a non-existent parent");
        const auto expected=NativeNetworkTargetAt(*selected,*metrics_policy,&*cold_historical.history);
        need(expected&&point["merit_target_q16"].getInt<uint64_t>()==expected->header_q16&&
             point["merit_target_numerator"].get_str()==expected->numerator.convert_to<std::string>()&&
             point["merit_target_denominator"].get_str()==expected->denominator.convert_to<std::string>(),
             "Historical graph does not use the exact native threshold");
    }
    const auto duty=InspectAutomaticNativeWorkDuty(*restored_self,*audit_tip->GetAncestor(9),GetScriptForDestination(WitnessV0KeyHash(known(2).GetPubKey())),NativeDutySpent{},0);need(duty.checked.proof&&duty.references.size()==2,"Self history cannot serve ordinary cycle");

    // Real native index ancestry and authenticated alternate report bytes.
    const auto second_report=commitment_for(hashes[1]);
    const auto report_b=make_block(parent,{CTxOut{0,*BuildNativeReportEnvelope(second_report)}},501);
    const auto report_b_hash=report_b.GetHash();CBlockIndex report_b_index{report_b};
    need(!historical.history->QuoteAt(4,report_b_hash).quote,"Historical quote accepted an orphan identity at the same height");
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
    std::cout<<"{\"PASS\":true,\"whole_state_comparisons\":"<<comparisons<<",\"native_signed_enrollments\":3,\"native_signed_retained_audit_events\":8,\"full_incremental_retained_history\":true,\"retained_receipt_reorg_restored\":true,\"canonical_hourly_quote_from_native_receipts\":true,\"missing_history_never_quotes\":true,\"future_window_nonzero\":true,\"native_policy_header_binding\":true,\"native_metrics_threshold_binding\":true,\"cold_loaded_header_mutations_refused\":true,\"historical_target_preserved\":true,\"missing_context_refused\":true,\"exact_decimal_header_encoding\":true,\"mathematics_tested\":false,\"reorg_and_restore\":true,\"warm_budget_rejections\":true,\"empty_archive_refused\":true}\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
