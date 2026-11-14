// Private known-key QA: real native signatures and audit/duty transitions.
// Synthetic zero counts do not qualify scientific mathematics or economic safety.
#undef NDEBUG
#include <pgn/native_work_duty.h>
#include <pgn/native_audit_settlement.h>
#include <common/signmessage.h>
#include <key.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <streams.h>
#include <util/translation.h>
#include <array>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace pgn::core;
extern const TranslateFn G_TRANSLATION_FUN=[](const char* text){return std::string(text);};
static void need(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
static uint256 Id(unsigned n){std::array<unsigned char,32> bytes{};bytes[0]=n&255;bytes[1]=(n>>8)&255;return uint256{bytes};}
struct Actor{
 CKey key;CScript script;
 explicit Actor(unsigned scalar){std::array<unsigned char,32> bytes{};bytes.back()=scalar;key.Set(bytes.begin(),bytes.end(),true);script=GetScriptForDestination(WitnessV0KeyHash(key.GetPubKey()));}
 Bytes sign(const std::string& challenge)const{
  auto status=MessageVerificationResult::ERR_INVALID;const auto pair=BIP322Txs::Create(WitnessV0KeyHash(key.GetPubKey()),challenge,status);need(pair.has_value(),"No BIP322 transaction");
  CMutableTransaction tx{pair->m_to_sign};FillableSigningProvider provider;provider.AddKey(key);SignatureData data;
  need(ProduceSignature(provider,MutableTransactionSignatureCreator(tx,0,pair->m_to_spend.vout[0].nValue,SignOptions{SIGHASH_ALL}),pair->m_to_spend.vout[0].scriptPubKey,data),"Signature creation failed");UpdateInput(tx.vin[0],data);
  DataStream wire;wire<<tx.vin[0].scriptWitness.stack;Bytes out(wire.size());std::memcpy(out.data(),wire.data(),wire.size());return out;
 }
};
struct Indexes{
 std::deque<uint256> hashes;std::deque<CBlockIndex> blocks;
 Indexes(){for(unsigned i=0;i<40;++i){hashes.push_back(Id(100+i));blocks.emplace_back();auto& block=blocks.back();block.phashBlock=&hashes.back();block.nHeight=i;block.nStatus=BLOCK_VALID_SCRIPTS;if(i)block.pprev=&blocks[i-1];block.BuildSkip();}}
};
static Bytes bytes(const std::string& text){return {text.begin(),text.end()};}
static std::string answer(const NativeReportAudit& report,uint8_t slot,const CBlockIndex& parent,bool wrong){
 const auto partitions=NativeAuditPartitions(report.report.commitment.root,parent.GetAncestor(report.report.height+1)->GetBlockHash());
 UniValue value{UniValue::VOBJ};value.pushKV("schema","PGN_AUDIT_ANSWER_V1");value.pushKV("ticket",HexStr(report.range.ticket_id));value.pushKV("attempt",0);UniValue indices{UniValue::VARR},counts{UniValue::VARR};
 for(auto index:partitions[slot])indices.push_back(uint64_t(index));for(unsigned i=0;i<5;++i)counts.push_back(wrong&&i==0?"1":"0");value.pushKV("partitions",indices);value.pushKV("counts",counts);value.pushKV("nonce",std::string(64,'a'));return value.write();
}
static unsigned run(unsigned miners,bool bad_answer=false){
 Actor owner(1),other(2),third(3);Indexes indexes;const auto chain=Id(1),request=Id(701);const auto& parent=indexes.blocks[8];
 NativeAuditLedger ledger;ledger.public_admission=true;ledger.audit_chain=chain;ledger.audit_operator_rules=NativeAuditOperatorRules{chain,9};ledger.publication_height=9;for(size_t n=0;n<indexes.blocks.size();++n)indexes.blocks[n].nTime=uint32_t(100*n);
 NativeReportAudit report;report.owner=owner.script;report.report.height=1;report.range={Digest{},Integer{"150000000000000000000"},Integer{"200000000000000"},100};report.range.ticket_id[0]=7;
 UniValue body{UniValue::VOBJ};body.pushKV("schema","PGN_NETWORK_RANGE_REPORT_V1");body.pushKV("genesis",chain.GetHex());body.pushKV("request_id",request.GetHex());body.pushKV("ticket",HexStr(report.range.ticket_id));body.pushKV("commitment_nonce",std::string(64,'b'));
 UniValue computed{UniValue::VOBJ},counts{UniValue::VARR},best{UniValue::VARR};for(unsigned i=0;i<100;++i)counts.push_back("0");computed.pushKV("schema","PGN_HORIZON_COMPUTED_REPORT_V1");computed.pushKV("start",report.range.start.convert_to<std::string>());computed.pushKV("end",(report.range.start+report.range.length).convert_to<std::string>());computed.pushKV("positions",report.range.length.convert_to<std::string>());computed.pushKV("ticket_id",HexStr(report.range.ticket_id));computed.pushKV("counts",counts);computed.pushKV("best",best);body.pushKV("computed",computed);
 const auto disclosure=bytes(body.write());report.report.commitment.root=*uint256::FromHex(HexStr(AuditSha(disclosure)));ledger.emplace(request,report);auto initial=ledger;
 const auto event=[&](const Actor& actor,NativeAuditAction action,uint8_t slot,Bytes payload){NativeAuditEvent e{request,parent.GetBlockHash(),action,slot,0,actor.script,std::move(payload)};const auto challenge=ScriptWorkChallenge({chain,NativeAuditWorkIdentity(chain,ledger.at(request).report.commitment.root,e),e.parent},e.actor,{4096,4096});need(challenge.has_value(),"No audit challenge");e.signature=actor.sign(*challenge);return e;};
 const auto refused=[&](const NativeAuditEvent& e){auto copy=ledger;bool no=false;try{ApplyNativeAuditEvent(copy,e,parent,chain,{4096,4096});}catch(...){no=true;}need(no,"Invalid signed transition accepted");};
 need(!ConfiguredNativeAuditOperatorRules(chain),"Production rule activated before qualification");need(NativeAuditRecheckAt(ledger,8)&&!NativeAuditRecheckAt(ledger,7),"Candidate-block activation boundary changed");
 auto legacy=ledger;legacy.audit_operator_rules.reset();need(!SelectNativeAudit(legacy,owner.script,parent.nHeight,0),"Legacy owner exclusion changed");need(!SelectNativeAudit(ledger,owner.script,7,0),"Activation bypassed");auto foreign=ledger;foreign.audit_chain=Id(999);need(!SelectNativeAudit(foreign,owner.script,parent.nHeight,0),"Foreign chain activated");
 const Actor* actors[2]={miners==1?&owner:&other,miners==3?&third:&owner};std::vector<NativeAuditEvent> accepted;
 for(uint8_t slot=0;slot<2;++slot){
  const auto picked=SelectNativeAudit(ledger,actors[slot]->script,parent.nHeight,0);need(picked&&picked->first==request&&picked->second==slot,"Automatic selection failed");
  auto e=event(*actors[slot],NativeAuditAction::Assign,slot,NativeTimedAuditAssignmentBody());auto forged=e;forged.signature.back()^=1;refused(forged);forged=e;forged.parent=Id(900);refused(forged);ApplyNativeAuditEvent(ledger,e,parent,chain,{4096,4096});accepted.push_back(e);
 }
 const auto owner_reveal=event(owner,NativeAuditAction::ReportReveal,0,disclosure);refused(owner_reveal);
 for(uint8_t slot=0;slot<2;++slot){
  auto text=answer(ledger.at(request),slot,parent,bad_answer&&slot==1);
  auto publication=event(*actors[slot],NativeAuditAction::Publish,slot,bytes(text));
  refused(publication); // Never disclose before this actor's own commitment.
  auto e=event(*actors[slot],NativeAuditAction::Commit,slot,ParseHex(HexStr(AuditSha(bytes(text)))));
  ApplyNativeAuditEvent(ledger,e,parent,chain,{4096,4096});accepted.push_back(e);
  auto inactive=ledger;inactive.publication_height.reset();bool rejected=false;
  try{ApplyNativeAuditEvent(inactive,publication,parent,chain,{4096,4096});}catch(const std::invalid_argument&){rejected=true;}
  need(rejected,"Unconfigured publication rule activated");
  ApplyNativeAuditEvent(ledger,publication,parent,chain,{4096,4096});accepted.push_back(publication);
  const auto& published=ledger.at(request);
  need(!published.expected&&published.jobs[slot]->stage==NativeAuditJob::Committed&&published.jobs[slot]->reveal==text,"Publication invented owner comparison");
  need(!NativeAuditReportQualified(ledger,published)&&AuditSettlementTaskCommitment(ledger,0,10).first==0,"Publication alone became payable");
  if(slot==0)need(published.jobs[1]->stage==NativeAuditJob::Assigned,"Publication waited for absent other auditor");
  ApplyNativeAuditEvent(ledger,publication,parent,chain,{4096,4096});
  need(ledger.at(request).jobs[slot]->passed_height==0,"Duplicate publication invented Passed height");
  DataStream encoded;native_audit_replay_detail::WriteCheckpointLedger(encoded,ledger);
  auto recovered=native_audit_replay_detail::ReadCheckpointLedger(encoded);
  need(encoded.empty()&&recovered.publication_height==ledger.publication_height&&recovered.at(request).jobs[slot]->reveal==text&&
       recovered.at(request).jobs[slot]->stage==NativeAuditJob::Committed&&!recovered.at(request).expected&&
       recovered.at(request).jobs[slot]->committed_height==published.jobs[slot]->committed_height&&
       recovered.at(request).jobs[slot]->committed_parent_time==published.jobs[slot]->committed_parent_time,"Published body lost at checkpoint restart");
  need(!NativeAuditOwnerAbandoned(ledger,published,indexes.blocks[14]),"Owner grace expired before 600 canonical seconds");
  need(NativeAuditOwnerAbandoned(ledger,published,indexes.blocks[15])==(slot==1),"Owner window began before both commits or missed its boundary");
  need(!NativeAuditOwnerAbandoned(inactive,published,indexes.blocks[30]),"Legacy history reinterpreted as abandoned");
 }
 auto late=ledger;const auto& late_parent=indexes.blocks[30];
 need(NativeAuditOwnerAbandoned(late,late.at(request),late_parent),"Owner did not become explicitly abandoned");
 auto late_reveal=owner_reveal;late_reveal.parent=late_parent.GetBlockHash();
 const auto late_challenge=ScriptWorkChallenge({chain,NativeAuditWorkIdentity(chain,late.at(request).report.commitment.root,late_reveal),late_reveal.parent},late_reveal.actor,{4096,4096});
 need(late_challenge.has_value(),"Late owner signing context unavailable");late_reveal.signature=owner.sign(*late_challenge);
 ApplyNativeAuditEvent(late,late_reveal,late_parent,chain,{4096,4096});
 need(NativeAuditReportQualified(late,late.at(request))==!bad_answer,"Late owner did not adjudicate retained bodies");
 for(uint8_t slot=0;slot<2;++slot){
  need(late.at(request).jobs[slot]->reveal==ledger.at(request).jobs[slot]->reveal,"Owner timeout destroyed audit body");
  need(late.at(request).jobs[slot]->committed_height==ledger.at(request).jobs[slot]->committed_height,"Late owner rewrote commitment receipt");
 }
 need(!NativeAuditOwnerAbandoned(late,late.at(request),late_parent),"Verified late owner remained abandoned");

 need(!ledger.at(request).expected,"Expected answers disclosed too early");ApplyNativeAuditEvent(ledger,owner_reveal,parent,chain,{4096,4096});accepted.push_back(owner_reveal);
 for(uint8_t slot=0;slot<2;++slot){auto e=event(*actors[slot],NativeAuditAction::Reveal,slot,bytes(answer(ledger.at(request),slot,parent,bad_answer&&slot==1)));auto forged=e;forged.body.back()^=1;refused(forged);ApplyNativeAuditEvent(ledger,e,parent,chain,{4096,4096});accepted.push_back(e);const auto passed=ledger.at(request).jobs[slot]->passed_height;ApplyNativeAuditEvent(ledger,e,parent,chain,{4096,4096});need(ledger.at(request).jobs[slot]->passed_height==passed,"Duplicate reveal changed receipt");}
 const auto& done=ledger.at(request);need(NativeAuditReportQualified(ledger,done)==!bad_answer,"Wrong audit qualification");need(NativeAuditOperatorsIndependent(ledger,done)==(miners==3),"False independence label");
 auto replay=initial;for(const auto& e:accepted)ApplyNativeAuditEvent(replay,e,parent,chain,{4096,4096});need(replay.at(request).owner_reveal==done.owner_reveal&&replay.at(request).jobs[0]->reveal==done.jobs[0]->reveal&&replay.at(request).jobs[1]->reveal==done.jobs[1]->reveal,"Replay lost receipt");
 const auto tasks=AuditSettlementTaskCommitment(ledger,0,10);
 need(tasks.first==(bad_answer?1:2),"Failed answer entered payable tasks");
 if(bad_answer)return accepted.size();
 constexpr CAmount coin=PGN_ATOMIC_UNITS_PER_COIN;
 const GoldEpochDefinition definition{200000,10,171*coin,32*coin,24*coin,8*coin,125*coin,1000*coin,40*65536};
 const auto pot=*QuoteCumulativeAuditAccrual(definition,10);
 IssuanceSnapshot before{Id(110),PGN_MAX_SUPPLY_ATOMIC,0,pot,IssuanceBudgetPartition{Id(500),PGN_MAX_SUPPLY_ATOMIC,0,pot,0,0},OrdinaryRewardPools{0,pot},GoldEpochBalances{10,10,0,0,0,0},AuditSettlementBalances{}};
 need(before.Valid(),"Invalid issuance fixture");auto after=before;after.tip=Id(990+miners);
 const auto page=ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),after);
 need(page&&page->count==2&&page->outputs.size()==2&&page->paid==pot&&after.Valid(),"Native audit settlement did not close");
 CAmount paid{};for(const auto& output:page->outputs){need(output.nValue>0,"Invalid payout");paid+=output.nValue;}
 need(paid==pot&&after.minted-before.minted==pot&&after.promised==0&&after.pools->audit==0,"Audit pot was not conserved");
 if(miners==1)need(page->outputs[0].scriptPubKey==owner.script&&page->outputs[1].scriptPubKey==owner.script,"Self audit paid another recipient");
 auto duplicate=after;const auto empty=ApplyAuditSettlementPage(ledger,definition,2,10,10,Id(110),duplicate);
 need(empty&&empty->count==0&&empty->paid==0&&duplicate==after,"Duplicate audit payment");
 auto replay_money=before;replay_money.tip=after.tip;const auto replay_page=ApplyAuditSettlementPage(replay,definition,2,10,10,Id(110),replay_money);
 need(replay_page&&replay_page->outputs==page->outputs&&replay_money==after,"Replayed financial consequence differs");

 NativeDutySpent spent;const auto& tip=indexes.blocks[9];auto duty=InspectAutomaticNativeWorkDuty(ledger,tip,actors[1]->script,spent,0);need(duty.checked.proof.has_value()&&!duty.references.empty(),"Delivered self/other audit cannot serve normal cycle");
 need(std::string(NativeAuditOperatorAssurance(ledger,done))==(miners==3?"distinct_operators":"self_or_repeated_operator"),"Assurance mislabeled");
 auto historical=ledger;historical.at(request).jobs[1]->issued=8;
 need(NativeAuditReportQualified(historical,historical.at(request))==(miners==3),"Preactivation self assignment retroactively qualified");
 need(duty.references.size()==(miners==1?2:1),"Duty delivery count changed");need(ConsumeNativeWorkDuty(*duty.checked.proof,tip.GetBlockHash(),actors[1]->script,Id(900+miners),spent),"Duty consumption failed");need(!ConsumeNativeWorkDuty(*duty.checked.proof,tip.GetBlockHash(),actors[1]->script,Id(950+miners),spent),"Audit credit reused");
 return accepted.size();
}
static void preferences(){
 Actor a(1),b(2),alias(4);NativeAuditLedger ledger;ledger.public_admission=true;ledger.audit_chain=Id(1);ledger.audit_operator_rules=NativeAuditOperatorRules{Id(1),9};
 NativeReportAudit own,other;own.owner=a.script;own.report.height=1;other.owner=b.script;other.report.height=2;ledger.emplace(Id(1),own);ledger.emplace(Id(2),other);
 auto picked=SelectNativeAudit(ledger,a.script,8,0);need(picked&&picked->first==Id(2),"Own older report outranked available other work");
 ledger.at(Id(1)).jobs[0]=NativeAuditJob{b.script,0,9,100,NativeAuditJob::Committed};ledger.at(Id(2)).jobs[0]=NativeAuditJob{a.script,0,9,100,NativeAuditJob::Committed};
 picked=SelectNativeAudit(ledger,a.script,8,0);need(picked&&picked->first==Id(1)&&picked->second==1,"Distinct-operator fallback was not preferred");
 ledger.participants.emplace(Bytes{a.script.begin(),a.script.end()},NativeAuditMembership{Id(50),true});ledger.participants.emplace(Bytes{alias.script.begin(),alias.script.end()},NativeAuditMembership{Id(50),true});
 ledger.at(Id(2)).jobs[1]=NativeAuditJob{alias.script,0,9,100,NativeAuditJob::Committed};need(!NativeAuditOperatorsIndependent(ledger,ledger.at(Id(2))),"Alias fabricated independence");
 ledger.at(Id(2)).jobs[1].reset();
 for(unsigned n=300;n<1300;++n){NativeReportAudit fresh;fresh.owner=b.script;fresh.report.height=3;ledger.emplace(Id(n),fresh);}
 picked=SelectNativeAudit(ledger,a.script,8,0);need(picked&&picked->first==Id(1)&&picked->second==1,"Fresh work starved ready report closure");

}
int main(){try{
 need(!ConfiguredNativeAuditPublicationHeight(Id(1)),"Candidate accidentally configured");
 NativeAuditLedger empty;DataStream old;
 old<<uint8_t{1}<<empty.authority<<empty.authority_request<<empty.authority_height<<empty.public_admission<<empty.audit_chain<<false<<uint64_t{0}<<uint64_t{0};
 auto legacy=native_audit_replay_detail::ReadCheckpointLedger(old);
 need(old.empty()&&!legacy.publication_height,"Version-one checkpoint migration changed policy");
 DataStream v2;v2<<uint8_t{2}<<empty.authority<<empty.authority_request<<empty.authority_height<<empty.public_admission<<empty.audit_chain<<false<<true<<int32_t{9}<<uint64_t{0}<<uint64_t{0};
 auto previous=native_audit_replay_detail::ReadCheckpointLedger(v2);
 need(v2.empty()&&previous.publication_height==9,"Version-two checkpoint migration changed policy");
 ECC_Context ecc;preferences();unsigned events{};for(unsigned miners:{1u,2u,3u})events+=run(miners);events+=run(1,true);std::cout<<"{\"PASS\":true,\"publication_before_other_commit\":true,\"owner_absence_does_not_hold_body\":true,\"no_Passed_or_payment_from_publication\":true,\"deferred_adjudication\":true,\"canonical_owner_abandoned\":true,\"late_owner_retained_bodies\":true,\"checkpoint_v1_v2_v3\":true,\"network_activation_disabled\":true,\"signed_events\":"<<events<<",\"one_two_three_operator_cycles\":true,\"answer_disagreement_refused\":true,\"production_headers_tested\":true,\"candidate_height_boundary\":true,\"preactivation_self_receipt_not_reinterpreted\":true,\"real_BIP322_signatures\":true,\"replay_preserved\":true,\"one_use_duties\":true,\"production_changed\":false,\"blindness_qualified\":false,\"native_Gold_block_tested\":false,\"native_financial_kernel_tested\":true,\"pot_conserved\":true,\"duplicate_payment_prevented\":true,\"other_work_preferred\":true,\"ready_report_not_starved_by_1000_new_reports\":true,\"aliases_not_independent\":true}\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
