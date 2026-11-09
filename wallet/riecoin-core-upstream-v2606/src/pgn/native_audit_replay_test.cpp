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

    // Signed actor credits are replayed with the audit prefix, never separately.
    struct DutyCase {
        uint256 hash;CBlockIndex index;
        std::unique_ptr<CCoinsViewDB> coins;
        std::unique_ptr<CCoinsViewCache> view;
        DutyCase(const CBlock& block,CBlockIndex& prior,const uint256& chain,const fs::path& path)
            :hash(block.GetHash()),index(block),
             coins(std::make_unique<CCoinsViewDB>(DBParams{.path=path,.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},CoinsViewOptions{})),
             view(std::make_unique<CCoinsViewCache>(coins.get()))
        {
            index.phashBlock=&hash;index.pprev=&prior;index.nHeight=prior.nHeight+1;
            index.nTx=1;index.nStatus=BLOCK_VALID_SCRIPTS;index.BuildSkip();
            view->SetBestBlock(hash);view->StagePgnFactCommit({{},FactHead{chain,hash},{}});
        }
    };
    std::vector<std::unique_ptr<DutyCase>> duty_cases;
    const auto append=[&](CBlockIndex& prior,std::vector<CTxOut> outputs){
        auto block=make_block(prior,std::move(outputs),unsigned(700+duty_cases.size()));
        auto next=std::make_unique<DutyCase>(block,prior,chain,path/fs::PathFromString("duty-"+std::to_string(duty_cases.size())));
        need(history.StoreBlock(*next->view,next->index,next->index.nHeight,block,replay.archive)==RequestArchiveVerdict::Stored,"Duty fixture archive");
        auto* result=next.get();duty_cases.push_back(std::move(next));return result;
    };
    const auto signed_audit=[&](const CBlockIndex& prior,unsigned scalar,uint8_t slot,NativeAuditAction action){
        const auto actor_key=known(scalar);
        NativeAuditEvent event;event.request=first_report.request;event.parent=prior.GetBlockHash();
        event.action=action;event.slot=slot;event.actor=GetScriptForDestination(WitnessV0KeyHash(actor_key.GetPubKey()));
        if(action==NativeAuditAction::Commit)event.body=Bytes(32,uint8_t(scalar));
        const auto identity=NativeAuditWorkIdentity(chain,first_report.root,event);
        event.signature=sign(*ScriptWorkChallenge({chain,identity,event.parent},event.actor,signature_budget),actor_key);
        const auto script=BuildNativeAuditEnvelope(event);need(script.has_value(),"Signed fixture audit envelope");return CTxOut{0,*script};
    };
    const auto signed_request=[&](const CBlockIndex& prior,unsigned scalar){
        const auto actor_key=known(scalar);
        const auto actor=GetScriptForDestination(WitnessV0KeyHash(actor_key.GetPubKey()));
        TicketRequest request{chain,prior.GetBlockHash(),prior.nHeight,actor,actor};
        const auto identity=TicketRequestIdentity(request,request_budget);need(identity.has_value(),"Credit request identity");
        const auto signature=sign(*ScriptWorkChallenge({chain,*identity,request.anchor},actor,signature_budget),actor_key);
        const auto envelope=BuildTicketRequestEnvelope(request,signature,request_budget);
        need(envelope.envelope.has_value(),"Credit request signature");return CTxOut{0,envelope.envelope->Script()};
    };
    const NativeWorkDutyActivation activation{chain,hashes[0],hashes[0],0,6,6};
    unsigned duty_comparisons{};
    const auto compare_duty=[&](const CCoinsView& state,const CBlockIndex& tip,const NativeWorkDutyActivation& active,
                                RangeScheduleBudget budget,ScriptWorkBudget signature){
        const auto cached=ReadNativeWorkDutyState(state,tip,history,policy_budget,budget,signature,active);
        const auto full=ReadNativeWorkDutyStateUncached(state,tip,history,policy_budget,budget,signature,active);
        need(bool(cached)==bool(full)&&(!cached||(NativeAuditReplaySame(cached->audits,full->audits)&&cached->spent==full->spent)),
             "Incremental credit map diverged from complete native replay");
        ++duty_comparisons;return cached;
    };
    need(compare_duty(view_admitted,admitted_index,activation,schedule,signature_budget).has_value(),"Cold credit state");
    auto* first_credit=append(admitted_index,{signed_audit(admitted_index,2,0,NativeAuditAction::Assign),
                                            signed_audit(admitted_index,2,0,NativeAuditAction::Commit)});
    need(compare_duty(*first_credit->view,first_credit->index,activation,schedule,signature_budget).has_value(),"First signed credit");
    auto* second_credit=append(first_credit->index,{signed_audit(first_credit->index,3,1,NativeAuditAction::Assign),
                                                  signed_audit(first_credit->index,3,1,NativeAuditAction::Commit)});
    auto before_debit=compare_duty(*second_credit->view,second_credit->index,activation,schedule,signature_budget);
    need(before_debit&&before_debit->spent.empty(),"Answers were debited without a ticket request");
    auto* debited=append(second_credit->index,{signed_request(second_credit->index,2),signed_request(second_credit->index,3)});
    auto after_debit=compare_duty(*debited->view,debited->index,activation,schedule,signature_budget);
    need(after_debit&&after_debit->spent.size()==2,"Two signed ticket requests did not consume their distinct credits");
    need(compare_duty(*debited->view,debited->index,activation,schedule,signature_budget)->spent==after_debit->spent,
         "Exact parent reread debited credit twice");
    const auto& report=after_debit->audits.at(first_report.request);
    const auto claim=NativeDutyRef(first_report.request,0,*report.jobs[0]);
    const std::array<NativeDutyReference,1> claims{claim};
    const auto reused=InspectNativeWorkDuty(after_debit->audits,debited->index,report.jobs[0]->actor,claims,after_debit->spent,6);
    need(reused.verdict==NativeDutyVerdict::AlreadySpent&&!reused.proof,"Spent answer reused by another request");
    // Branch change must discard debits, and returning to the old branch restores them.
    need(compare_duty(*second_credit->view,second_credit->index,activation,schedule,signature_budget)->spent.empty(),"Debit leaked backwards across branch");
    need(compare_duty(*debited->view,debited->index,activation,schedule,signature_budget)->spent.size()==2,"Debit lost after branch restoration");
    auto later_activation=activation;later_activation.height=9;
    need(compare_duty(*debited->view,debited->index,later_activation,schedule,signature_budget)->spent.empty(),"Changed activation reused old credits");
    need(compare_duty(*debited->view,debited->index,activation,schedule,signature_budget)->spent.size()==2,"Original activation failed to reconstruct credits");
    auto changed_assignment=activation;changed_assignment.height=7;changed_assignment.first_assignment_height=7;
    need(compare_duty(*debited->view,debited->index,changed_assignment,schedule,signature_budget)->spent.size()==1,"Assignment-height boundary borrowed an earlier credit");
    auto wrong_genesis=activation;wrong_genesis.genesis=hashes[1];
    need(!compare_duty(*debited->view,debited->index,wrong_genesis,schedule,signature_budget),"Wrong activation genesis accepted");
    need(compare_duty(*debited->view,debited->index,activation,schedule,signature_budget)->spent.size()==2,"Credit state did not recover");
    auto tiny_credit=schedule;tiny_credit.archive.coinbase_bytes=1;
    need(!compare_duty(*debited->view,debited->index,activation,tiny_credit,signature_budget),"Warm credits bypassed archive budget");
    need(!compare_duty(*debited->view,debited->index,activation,schedule,too_small),"Warm credits bypassed signature budget");
    need(compare_duty(*debited->view,debited->index,activation,schedule,signature_budget)->spent.size()==2,"Warm credit recovery failed");
    // Same-block answer cannot pay for a request that must be checked first.
    auto* same_block=append(admitted_index,{signed_request(admitted_index,2),
        signed_audit(admitted_index,2,0,NativeAuditAction::Assign),signed_audit(admitted_index,2,0,NativeAuditAction::Commit)});
    need(!compare_duty(*same_block->view,same_block->index,activation,schedule,signature_budget),"Same-block answer supplied earlier request credit");
    need(compare_duty(*debited->view,debited->index,activation,schedule,signature_budget)->spent.size()==2,"Invalid branch poisoned valid credits");

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
    std::cout<<"{\"duty_state_comparisons\":"<<duty_comparisons<<",\"signed_credits\":2,\"request_debits\":2,\"same_block_credit_rejected\":true}\n";
    std::cout<<"{\"PASS\":true,\"whole_state_comparisons\":"<<comparisons<<",\"native_signed_enrollments\":2,\"reorg_and_restore\":true,\"warm_budget_rejections\":true,\"empty_archive_refused\":true}\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
