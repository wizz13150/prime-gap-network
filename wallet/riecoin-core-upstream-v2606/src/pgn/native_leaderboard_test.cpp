// Synthetic known-key native archive fixture. No wallet, live chain or network.
#include <pgn/native_leaderboard.h>
#include <pgn/leaderboard_snapshot_cache.h>
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
#include <util/translation.h>
#include <cstring>
#include <deque>
#include <iostream>
extern const TranslateFn G_TRANSLATION_FUN=[](const char* text){return std::string(text);};
using namespace pgn::core;
static unsigned checks{};
static void need(bool value,const char* label){if(!value)throw std::runtime_error(label);}
static void check(bool value,const char* label){need(value,label);++checks;std::cout<<"PASS "<<label<<std::endl;}
static CKey known(unsigned scalar){std::array<unsigned char,32> b{};b.back()=scalar;CKey k;k.Set(b.begin(),b.end(),true);return k;}
static CScript script(unsigned scalar){return GetScriptForDestination(WitnessV0KeyHash(known(scalar).GetPubKey()));}
static CKey keyFor(const CScript& s){for(unsigned i=1;i<=6;++i)if(script(i)==s)return known(i);throw std::runtime_error("Unknown synthetic signer");}
static Bytes sign(const std::string& message,const CKey& key){
 auto status=MessageVerificationResult::ERR_INVALID;const auto pair=BIP322Txs::Create(WitnessV0KeyHash(key.GetPubKey()),message,status);need(bool(pair),"BIP322 challenge");
 CMutableTransaction tx{pair->m_to_sign};FillableSigningProvider provider;provider.AddKey(key);SignatureData data;
 need(ProduceSignature(provider,MutableTransactionSignatureCreator(tx,0,pair->m_to_spend.vout[0].nValue,SignOptions{SIGHASH_ALL}),pair->m_to_spend.vout[0].scriptPubKey,data),"Native signing");UpdateInput(tx.vin[0],data);
 DataStream wire;wire<<tx.vin[0].scriptWitness.stack;Bytes out(wire.size());std::memcpy(out.data(),wire.data(),wire.size());return out;
}
struct Fixture {
 fs::path path;uint256 chain;NativeRequestHistoryArchive archive;
 RangePolicyReadBudget policy{16,4096,16384,8192};
 TicketRequestBudget request{128,128,4096};
 ConfirmedRequestScanBudget scan{64,16,65536,1,request};
 RangeScheduleBudget schedule{64,100,100,{65536,65536},scan,{64,4096,1024}};
 ScriptWorkBudget signature{4096,4096};
 std::deque<uint256> hashes;std::vector<std::unique_ptr<CBlockIndex>> indices;
 std::vector<std::unique_ptr<CCoinsViewDB>> stores;std::vector<std::unique_ptr<CCoinsViewCache>> views;
 CBlockIndex* tip{};CCoinsViewCache* view{};
 explicit Fixture(fs::path p):path(std::move(p)),archive(path/"history",1<<20){chain=*uint256::FromHex("0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d");}
 CBlockIndex& Append(std::vector<CTxOut> outputs,CBlockIndex* parent=nullptr){
  if(!parent)parent=tip;CMutableTransaction tx;tx.vin.resize(1);tx.vin[0].prevout.SetNull();tx.vin[0].scriptSig=CScript{}<<int64_t(indices.size()+1)<<OP_0;tx.vout=std::move(outputs);
  CBlock block;block.nTime=100+indices.size()*100;if(parent)block.hashPrevBlock=parent->GetBlockHash();block.vtx={MakeTransactionRef(tx)};block.hashMerkleRoot=BlockMerkleRoot(block);hashes.push_back(block.GetHash());
  auto index=std::make_unique<CBlockIndex>(block);index->phashBlock=&hashes.back();index->pprev=parent;index->nHeight=parent?parent->nHeight+1:0;index->nTx=1;index->nStatus=BLOCK_VALID_SCRIPTS;index->BuildSkip();tip=index.get();indices.push_back(std::move(index));
  stores.push_back(std::make_unique<CCoinsViewDB>(DBParams{.path=path/fs::PathFromString("coins-"+std::to_string(indices.size())),.cache_bytes=1<<20,.memory_only=true,.wipe_data=false},CoinsViewOptions{}));
  views.push_back(std::make_unique<CCoinsViewCache>(stores.back().get()));view=views.back().get();view->SetBestBlock(tip->GetBlockHash());view->StagePgnFactCommit({{},FactHead{chain,tip->GetBlockHash()},{}});
  need(archive.StoreBlock(*view,*tip,tip->nHeight,block,schedule.archive)==RequestArchiveVerdict::Stored,"Authenticated coinbase archive");return *tip;
 }
 NativeAuditLedger Ledger(){auto value=ReadNativeAuditLedger(*view,*tip,archive,policy,schedule,signature);need(bool(value),"Native authenticated ledger reconstruction");return std::move(*value);}
 CScript Event(NativeAuditEvent event,const NativeAuditLedger& ledger){
  const auto root=event.action==NativeAuditAction::Enroll?uint256{}:ledger.at(event.request).report.commitment.root;
  event.parent=tip->GetBlockHash();event.signature=sign(*ScriptWorkChallenge({chain,NativeAuditWorkIdentity(chain,root,event),event.parent},event.actor,signature),keyFor(event.actor));
  const auto envelope=BuildNativeAuditEnvelope(event);need(bool(envelope),"Audit envelope");return *envelope;
 }
 LeaderboardBindings Bindings(const NativeAuditLedger& ledger){
  LeaderboardBindings result;NativeAuditLedger empty;
  if(!ledger.authority.empty())LeaderboardBind(result,Bytes{ledger.authority.begin(),ledger.authority.end()},NativeAuditOperator(empty,ledger.authority),ledger.authority_height);
  for(int32_t h=1;h<=tip->nHeight;++h){auto block=archive.Load(*view,*tip,h,schedule.archive);need(bool(block.block),"Binding authenticated history");
   for(const auto& out:block.block->Coinbase()->vout)if(IsNativeAuditScript(out.scriptPubKey)){auto event=DecodeNativeAuditEnvelope(out.scriptPubKey);need(bool(event),"Binding event");if(event->action!=NativeAuditAction::Enroll)continue;
    UniValue body;need(body.read(std::string{event->body.begin(),event->body.end()}),"Binding JSON");LeaderboardBind(result,ParseHex(body["signing_script"].get_str()),*uint256::FromHex(body["operator_id"].get_str()),h);
   }}return result;
 }
 NativeLeaderboardSnapshot Snapshot(const NativeAuditLedger& ledger){return BuildNativeLeaderboard(ledger,*tip,chain,this,Bindings(ledger));}
};
static UniValue Attribution(const std::string& name){UniValue out{UniValue::VOBJ};out.pushKV("schema","PGN_PUBLIC_ATTRIBUTION_V1");out.pushKV("name",name);return out;}
template<class Lease,class Ticket> static std::string Disclosure(Fixture& f,const Lease& lease,const Ticket& ticket,std::optional<UniValue> attribution){
 UniValue counts{UniValue::VARR};for(unsigned i=0;i<100;++i)counts.push_back("0");UniValue computed{UniValue::VOBJ};computed.pushKV("schema","PGN_HORIZON_COMPUTED_REPORT_V1");computed.pushKV("start",lease.assignment.start.template convert_to<std::string>());computed.pushKV("end",lease.assignment.end.template convert_to<std::string>());computed.pushKV("positions",(lease.assignment.end-lease.assignment.start).template convert_to<std::string>());computed.pushKV("ticket_id",HexStr(ticket));computed.pushKV("counts",counts);computed.pushKV("best",UniValue{UniValue::VARR});
 UniValue body{UniValue::VOBJ};body.pushKV("schema","PGN_NETWORK_RANGE_REPORT_V1");body.pushKV("genesis",f.chain.GetHex());body.pushKV("request_id",lease.owner.Id().GetHex());body.pushKV("ticket",HexStr(ticket));body.pushKV("commitment_nonce",std::string(64,'a'));body.pushKV("computed",computed);if(attribution)body.pushKV("discoverer",*attribution);return body.write();
}
// Additional real signed request/report/audit cycle for label transitions.
static uint256 AddLabeledReport(Fixture& f,unsigned owner,std::optional<UniValue> label,std::array<unsigned,2> auditors){
 TicketRequest request{f.chain,f.tip->GetBlockHash(),f.tip->nHeight,script(owner),script(owner)};const auto identity=TicketRequestIdentity(request,f.request);need(bool(identity),"Labeled request identity");const auto challenge=ScriptWorkChallenge({f.chain,*identity,request.anchor},request.miner,f.signature);auto envelope=BuildTicketRequestEnvelope(request,sign(*challenge,known(owner)),f.request);need(bool(envelope.envelope),"Labeled signed request");f.Append({CTxOut{0,envelope.envelope->Script()}});
 const auto included=f.archive.Load(*f.view,*f.tip,f.tip->nHeight,f.schedule.archive);need(bool(included.block),"Labeled native inclusion block");const auto confirmation=ScanConfirmedTicketRequests(*f.view,*f.tip,*included.block,f.scan);need(confirmation.batch&&confirmation.batch->Requests().size()==1,"Labeled request must be natively confirmed at its actual anchor height");
 // seed_delay=1, and allocation at h consumes only the known source at h-1:
 // inclusion H -> seed H+1 -> assignment H+2. Same schedule as initial fixture.
 f.Append({CTxOut{0,CScript{}<<OP_RETURN}});f.Append({CTxOut{0,CScript{}<<OP_RETURN}});
 const auto policy=ReadChainRangePolicy(*f.view,*f.tip,f.archive,f.schedule.archive,f.policy);need(bool(policy.policy),"Labeled authenticated policy");auto reservation=DeriveNativeCandidateReservation(*f.view,*f.tip,*policy.policy,f.archive,f.schedule,envelope.envelope->Request().Id());if(!reservation.reservation)throw std::runtime_error("Labeled native reservation verdict="+std::to_string(int(reservation.verdict))+" history="+(reservation.history?std::to_string(int(*reservation.history)):"absent")+" height="+std::to_string(f.tip->nHeight));const auto& lease=reservation.reservation->Lease();const auto id=lease.owner.Id();const auto disclosure=Disclosure(f,lease,reservation.reservation->Ticket(),std::move(label));const auto root=*uint256::FromHex(HexStr(AuditSha(Bytes{disclosure.begin(),disclosure.end()})));
 NativeReportCommitment report{id,reservation.reservation->Ticket(),root,f.tip->GetBlockHash(),{}};report.signature=sign(*ScriptWorkChallenge({f.chain,NativeReportWorkIdentity(f.chain,report),report.parent},script(owner),f.signature),known(owner));f.Append({CTxOut{0,*BuildNativeReportEnvelope(report)}});
 // Audits use a post-report block hash; do not select before it exists.
 f.Append({CTxOut{0,CScript{}<<OP_RETURN}});auto ledger=f.Ledger();
 for(auto actor:auditors){auto selected=SelectNativeAudit(ledger,script(actor),f.tip->nHeight,f.tip->GetMedianTimePast());need(selected&&selected->first==id,"Labeled automatic audit selection");NativeAuditEvent event{id,{},NativeAuditAction::Assign,selected->second,0,script(actor),NativeTimedAuditAssignmentBody()};f.Append({CTxOut{0,f.Event(event,ledger)}});ledger=f.Ledger();}
 std::array<std::string,2> answers;std::vector<CTxOut> events;const auto& state=ledger.at(id);
 for(unsigned slot=0;slot<2;++slot){const auto parts=NativeAuditPartitions(root,f.tip->GetAncestor(state.report.height+1)->GetBlockHash());UniValue positions{UniValue::VARR},counts{UniValue::VARR};for(auto part:parts[slot]){positions.push_back(uint64_t(part));counts.push_back("0");}UniValue body{UniValue::VOBJ};body.pushKV("schema","PGN_AUDIT_ANSWER_V1");body.pushKV("ticket",HexStr(state.range.ticket_id));body.pushKV("attempt",0);body.pushKV("partitions",positions);body.pushKV("counts",counts);body.pushKV("nonce",std::string(64,'b'));answers[slot]=body.write();NativeAuditEvent event{id,{},NativeAuditAction::Commit,uint8_t(slot),0,state.jobs[slot]->actor,ParseHex(HexStr(AuditSha(Bytes{answers[slot].begin(),answers[slot].end()})))};events.emplace_back(0,f.Event(event,ledger));}
 f.Append(std::move(events));ledger=f.Ledger();NativeAuditEvent reveal{id,{},NativeAuditAction::ReportReveal,0,0,script(owner),Bytes{disclosure.begin(),disclosure.end()}};f.Append({CTxOut{0,f.Event(reveal,ledger)}});ledger=f.Ledger();events.clear();
 for(unsigned slot=0;slot<2;++slot){NativeAuditEvent event{id,{},NativeAuditAction::Reveal,uint8_t(slot),0,ledger.at(id).jobs[slot]->actor,Bytes{answers[slot].begin(),answers[slot].end()}};events.emplace_back(0,f.Event(event,ledger));}f.Append(std::move(events));ledger=f.Ledger();need(NativeAuditReportQualified(ledger,ledger.at(id)),"Labeled report finalized by native rules");return id;
}
int main(int argc,char** argv){try{
 need(argc==2,"Private fixture path required");ECC_Context ecc;LOCK(cs_main);Fixture f(fs::PathFromString(argv[1]));
 const Integer lower{"150000000000000000000"};const EncodingLimits encoding{10002,1048576,32,1};
 const CommittedGapPolicy evidence{encoding,{{128,32,32},10000},{256,256,256},{128,64},1,1};
 const RangePoolDefinition pool{lower,Integer{"1000000000000000000"},16,1,10};const CAmount unit=PGN_ATOMIC_UNITS_PER_COIN;
 const MonetaryPolicyDefinition money{99000000*unit,1000000*unit,{200*unit,1},100,70,25,5,FeeRoutingDefinition{100,100}};
 const auto range=BuildCandidateRangePolicyEnvelope(pool,f.policy),science=BuildCandidateSciencePolicyEnvelope(ScienceVerificationDefinition{1,evidence,encoding},f.policy),monetary=BuildCandidateMonetaryPolicyEnvelope(money,f.policy);
 need(range&&science&&monetary,"Native policies");f.Append({CTxOut{0,*range},CTxOut{0,*science},CTxOut{0,*monetary}});
 std::vector<VerifiedTicketRequestEnvelope> envelopes;
 for(unsigned scalar=1;scalar<=2;++scalar){TicketRequest request{f.chain,f.tip->GetBlockHash(),0,script(scalar),script(scalar)};const auto id=TicketRequestIdentity(request,f.request);need(bool(id),"Ticket identity");const auto challenge=ScriptWorkChallenge({f.chain,*id,request.anchor},request.miner,f.signature);auto envelope=BuildTicketRequestEnvelope(request,sign(*challenge,known(scalar)),f.request);need(bool(envelope.envelope),"Signed ticket");envelopes.push_back(*envelope.envelope);}
 f.Append({CTxOut{0,envelopes[0].Script()},CTxOut{0,envelopes[1].Script()}});f.Append({CTxOut{0,CScript{}<<OP_RETURN}});f.Append({CTxOut{0,CScript{}<<OP_RETURN}});
 const auto policy=ReadChainRangePolicy(*f.view,*f.tip,f.archive,f.schedule.archive,f.policy);need(bool(policy.policy),"Authenticated range policy");
 std::map<uint256,std::string> disclosures;std::vector<CTxOut> reports;
 for(const auto& envelope:envelopes){auto reservation=DeriveNativeCandidateReservation(*f.view,*f.tip,*policy.policy,f.archive,f.schedule,envelope.Request().Id());need(bool(reservation.reservation),"Authentic lease");const auto& lease=reservation.reservation->Lease();
  UniValue counts{UniValue::VARR};for(unsigned i=0;i<100;++i)counts.push_back("0");UniValue computed{UniValue::VOBJ};computed.pushKV("schema","PGN_HORIZON_COMPUTED_REPORT_V1");computed.pushKV("start",lease.assignment.start.convert_to<std::string>());computed.pushKV("end",lease.assignment.end.convert_to<std::string>());computed.pushKV("positions",(lease.assignment.end-lease.assignment.start).convert_to<std::string>());computed.pushKV("ticket_id",HexStr(reservation.reservation->Ticket()));computed.pushKV("counts",counts);computed.pushKV("best",UniValue{UniValue::VARR});
  UniValue body{UniValue::VOBJ};body.pushKV("schema","PGN_NETWORK_RANGE_REPORT_V1");body.pushKV("genesis",f.chain.GetHex());body.pushKV("request_id",lease.owner.Id().GetHex());body.pushKV("ticket",HexStr(reservation.reservation->Ticket()));body.pushKV("commitment_nonce",std::string(64,'a'));body.pushKV("computed",computed);body.pushKV("discoverer",Attribution("Shared Miner"));const auto text=body.write();disclosures.emplace(lease.owner.Id(),text);
  const auto root=*uint256::FromHex(HexStr(AuditSha(Bytes{text.begin(),text.end()})));NativeReportCommitment report{lease.owner.Id(),reservation.reservation->Ticket(),root,f.tip->GetBlockHash(),{}};report.signature=sign(*ScriptWorkChallenge({f.chain,NativeReportWorkIdentity(f.chain,report),report.parent},lease.owner.Request().miner,f.signature),keyFor(lease.owner.Request().miner));reports.emplace_back(0,*BuildNativeReportEnvelope(report));
 }
 f.Append(std::move(reports));auto ledger=f.Ledger();check(ledger.size()==2,"two signed miners reports reconstructed from actual native archive");
 auto before=f.Snapshot(ledger);check(before.rows.size()==2&&before.rows[0].tickets_reported==1&&before.rows[1].tickets_reported==1,"reported tickets counted exactly once per owner");
 NativeAuditLedger empty;const auto first_id=NativeAuditOperator(empty,script(1)),second_id=NativeAuditOperator(empty,script(2));std::vector<CTxOut> enrollments;
 for(unsigned scalar=1;scalar<=15;++scalar){if(scalar==5||scalar==6)continue;const auto id=scalar==3||scalar>=7?first_id:scalar==4?second_id:NativeAuditOperator(empty,script(scalar));UniValue body{UniValue::VOBJ};body.pushKV("schema","PGN_TESTNET_PARTICIPANT_V1");body.pushKV("operator_id",id.GetHex());body.pushKV("signing_script",HexStr(script(scalar)));body.pushKV("may_audit",true);const auto text=body.write();NativeAuditEvent event{ledger.authority_request,{},NativeAuditAction::Enroll,0,0,ledger.authority,Bytes{text.begin(),text.end()}};enrollments.emplace_back(0,f.Event(event,ledger));}
 f.Append(std::move(enrollments));ledger=f.Ledger();auto admitted=f.Snapshot(ledger);const auto first_row=std::find_if(admitted.rows.begin(),admitted.rows.end(),[&](const auto& r){return r.operator_id==first_id;});check(admitted.rows.size()==2&&first_row!=admitted.rows.end()&&first_row->signing_scripts.size()==11,"authenticated aliases merge by operator id without roster-only rows");
 auto alias_page=NativeLeaderboardPage(admitted,0,100);bool truncated=false;for(const auto& row:alias_page["rows"].getValues())if(row["operator_id"].get_str()==first_id.GetHex())truncated=row["signing_scripts"].size()==8&&row["signing_scripts_total"].getInt<unsigned>()==11&&row["signing_scripts_more"].get_bool();check(truncated,"eight-alias display bound explicit while all aliases count");
 // Four assignments selected by the existing native policy, never hand-set.
 for(unsigned i=0;i<4;++i){const unsigned actor=5+i%2;auto selected=SelectNativeAudit(ledger,script(actor),f.tip->nHeight,f.tip->GetMedianTimePast());need(bool(selected),"Automatic audit selection");NativeAuditEvent event{selected->first,{},NativeAuditAction::Assign,selected->second,0,script(actor),NativeTimedAuditAssignmentBody()};f.Append({CTxOut{0,f.Event(event,ledger)}});ledger=f.Ledger();}
 std::map<std::pair<uint256,unsigned>,std::string> answers;std::vector<CTxOut> commits;
 for(const auto& [id,report]:ledger)for(unsigned slot=0;slot<2;++slot){const auto parts=NativeAuditPartitions(report.report.commitment.root,f.tip->GetAncestor(report.report.height+1)->GetBlockHash());UniValue positions{UniValue::VARR},counts{UniValue::VARR};for(auto p:parts[slot]){positions.push_back(uint64_t(p));counts.push_back("0");}UniValue body{UniValue::VOBJ};body.pushKV("schema","PGN_AUDIT_ANSWER_V1");body.pushKV("ticket",HexStr(report.range.ticket_id));body.pushKV("attempt",0);body.pushKV("partitions",positions);body.pushKV("counts",counts);body.pushKV("nonce",std::string(64,'b'));const auto text=body.write();answers.emplace(std::pair{id,slot},text);NativeAuditEvent event{id,{},NativeAuditAction::Commit,uint8_t(slot),0,report.jobs[slot]->actor,ParseHex(HexStr(AuditSha(Bytes{text.begin(),text.end()})))};commits.emplace_back(0,f.Event(event,ledger));}
 f.Append(std::move(commits));ledger=f.Ledger();std::vector<CTxOut> reveals;
 for(const auto& [id,report]:ledger){const auto& text=disclosures.at(id);NativeAuditEvent event{id,{},NativeAuditAction::ReportReveal,0,0,report.owner,Bytes{text.begin(),text.end()}};reveals.emplace_back(0,f.Event(event,ledger));}f.Append(std::move(reveals));ledger=f.Ledger();auto* fork_parent=f.tip;
 reveals.clear();for(const auto& [id,report]:ledger)for(unsigned slot=0;slot<2;++slot){const auto& text=answers.at({id,slot});NativeAuditEvent event{id,{},NativeAuditAction::Reveal,uint8_t(slot),0,report.jobs[slot]->actor,Bytes{text.begin(),text.end()}};reveals.emplace_back(0,f.Event(event,ledger));}
 f.Append(std::move(reveals));ledger=f.Ledger();auto snapshot=f.Snapshot(ledger);
 check(snapshot.rows.size()==4,"productive owners and finalized audit actors included");
 check(std::count_if(snapshot.rows.begin(),snapshot.rows.end(),[](const auto& row){return row.display_name==std::optional<std::string>{"Shared Miner"};})==2,"homonymous signed labels remain distinct canonical operators");
 check(std::count_if(snapshot.rows.begin(),snapshot.rows.end(),[](const auto& row){return !row.display_name;})==2,"audit-only actors without disclosed labels remain unnamed");
 check(snapshot.rows[0].tickets_validated==1&&snapshot.rows[1].tickets_validated==1&&snapshot.rows[0].validated_search_positions>Integer{9007199254740991LL},"qualified search volume is exact above JSON safe integer");
 uint64_t passed=0;for(const auto& row:snapshot.rows)passed+=row.audits_finalized_valid;check(passed==4,"four cryptographically verified native finalized audits attributed once");
 auto page=NativeLeaderboardPage(snapshot,0,2),next=NativeLeaderboardPage(snapshot,2,2),last=NativeLeaderboardPage(snapshot,4,2);
 check(page["rows"].size()==2&&page["more"].get_bool()&&next["rows"].size()==2&&!next["more"].get_bool()&&last["rows"].empty(),"bounded pages cover ranked operators once");
 check(snapshot.rows[0].validated_search_positions>=snapshot.rows[1].validated_search_positions&&(!(snapshot.rows[0].validated_search_positions==snapshot.rows[1].validated_search_positions)||snapshot.rows[0].operator_id.GetHex()<snapshot.rows[1].operator_id.GetHex()),"volume descending and canonical hex operator-id tie order explicit");
 check(page["unavailable_fields"].isObject()&&!page["rows"][0].exists("active_workers")&&!page["rows"][0].exists("mining_seconds")&&!page["rows"][0].exists("online"),"unmeasured worker time and presence absent with reasons");
 auto all_names=NativeLeaderboardPage(snapshot,0,100);unsigned absent_names=0;for(const auto& row:all_names["rows"].getValues())if(!row.exists("display_name"))++absent_names;check(absent_names==2,"API omits absent display names rather than fabricating a username");
 bool rejected=false;try{NativeLeaderboardPage(snapshot,0,101);}catch(...){rejected=true;}check(rejected,"page larger than one hundred rejected");
 // Capture the REAL ledger/head, then advance the actual native index/archive
 // before completing the cold projection. Moving CoinsTip is a negative control.
 auto projection_ledger=std::make_shared<const NativeAuditLedger>(ledger);const auto* projection_anchor=f.tip;auto projection_bindings=f.Bindings(ledger);
 auto pinned_view=LeaderboardHistoryAnchorView::Capture(*f.view,*projection_anchor);CCoinsViewBacked moving_view{f.view};CChain canonical;canonical.SetTip(*f.tip);
 LeaderboardSnapshotCache cache;auto old_snapshot=std::make_shared<const NativeLeaderboardSnapshot>(snapshot);cache.Publish(old_snapshot,canonical);
 reveals.clear();for(const auto& [id,report]:ledger)for(unsigned slot=0;slot<2;++slot){const auto& text=answers.at({id,slot});NativeAuditEvent event{id,{},NativeAuditAction::Reveal,uint8_t(slot),0,report.jobs[slot]->actor,Bytes{text.begin(),text.end()}};reveals.emplace_back(0,f.Event(event,ledger));}f.Append(std::move(reveals));ledger=f.Ledger();auto repeated=f.Snapshot(ledger);uint64_t repeated_passed=0;Integer repeated_volume{},original_volume{};for(const auto& row:repeated.rows){repeated_passed+=row.audits_finalized_valid;repeated_volume+=row.validated_search_positions;}for(const auto& row:snapshot.rows)original_volume+=row.validated_search_positions;
 check(repeated_passed==4&&repeated_volume==original_volume,"repeated signed finalized receipts cannot double count audits or volume");
 canonical.SetTip(*f.tip);moving_view.SetBackend(*f.view);
 const auto moving_read=f.archive.Load(moving_view,*projection_anchor,projection_anchor->nHeight,f.schedule.archive);
 const auto pinned_read=f.archive.Load(*pinned_view,*projection_anchor,projection_anchor->nHeight,f.schedule.archive);
 check(!moving_read.block&&pinned_read.block.has_value(),"real archive read at old anchor survives extension using captured authenticated head");
 auto completed=BuildNativeLeaderboard(*projection_ledger,*projection_anchor,f.chain,&f,std::move(projection_bindings));
 check(completed.tip==snapshot.tip&&completed.height==snapshot.height&&NativeLeaderboardPage(completed,0,2).write()==page.write(),"block arriving during cold projection preserves coherent original snapshot");
 cache.BeginBuild(&f,f.chain,canonical);cache.Publish(std::make_shared<const NativeLeaderboardSnapshot>(repeated),canonical);
 auto old_page=cache.Get(&f,snapshot.tip,f.chain,canonical),new_page=cache.Get(&f,repeated.tip,f.chain,canonical);
 check(old_page&&new_page&&NativeLeaderboardPage(*old_page,2,2)["bestblockhash"].get_str()==snapshot.tip.GetHex()&&old_page->height!=new_page->height,"old-anchor pagination remains available beside new snapshot after extension");
 uint256 foreign_chain;foreign_chain.begin()[0]=88;check(!cache.Get(&f,snapshot.tip,foreign_chain,canonical)&&!cache.Get(&ledger,snapshot.tip,f.chain,canonical),"snapshot cache never crosses chain or chainstate identity");
 // A real signed enrollment AFTER the receipts must not remap those historic
 // audit counts. Public native admission allowed this previously unenrolled
 // synthetic actor to audit; its new ID is distinct from both owners.
 AddLabeledReport(f,6,Attribution("Historic Six"),{3,5});ledger=f.Ledger();
 uint256 late_id;late_id.begin()[0]=99;UniValue late_body{UniValue::VOBJ};late_body.pushKV("schema","PGN_TESTNET_PARTICIPANT_V1");late_body.pushKV("operator_id",late_id.GetHex());late_body.pushKV("signing_script",HexStr(script(6)));late_body.pushKV("may_audit",true);const auto late_text=late_body.write();NativeAuditEvent late_event{ledger.authority_request,{},NativeAuditAction::Enroll,0,0,ledger.authority,Bytes{late_text.begin(),late_text.end()}};f.Append({CTxOut{0,f.Event(late_event,ledger)}});ledger=f.Ledger();auto after_late=f.Snapshot(ledger);const auto historic_auditor=NativeAuditOperator(empty,script(6));const auto historic_row=std::find_if(after_late.rows.begin(),after_late.rows.end(),[&](const auto& r){return r.operator_id==historic_auditor;});
 check(NativeAuditOperator(ledger,script(6))==late_id&&historic_row!=after_late.rows.end()&&historic_row->audits_finalized_valid==2&&std::none_of(after_late.rows.begin(),after_late.rows.end(),[&](const auto& r){return r.operator_id==late_id;}),"native signed later enrollment cannot move earlier finalized audit counts");
 check(historic_row->display_name==std::optional<std::string>{"Historic Six"},"later enrollment cannot retroactively move a signed public label");
 canonical.SetTip(*f.tip);LeaderboardSnapshotCache eviction;eviction.Publish(old_snapshot,canonical);eviction.BeginBuild(&f,f.chain,canonical);eviction.Publish(new_page,canonical);eviction.BeginBuild(&f,f.chain,canonical);eviction.Publish(std::make_shared<const NativeLeaderboardSnapshot>(after_late),canonical);
 check(!eviction.Get(&f,snapshot.tip,f.chain,canonical)&&eviction.Get(&f,repeated.tip,f.chain,canonical)&&eviction.Get(&f,after_late.tip,f.chain,canonical),"two-slot retention evicts only oldest anchor with explicit cache miss");
 AddLabeledReport(f,3,Attribution("Renamed Miner"),{5,6});ledger=f.Ledger();auto renamed=f.Snapshot(ledger);const auto renamed_row=std::find_if(renamed.rows.begin(),renamed.rows.end(),[&](const auto& row){return row.operator_id==first_id;});check(renamed_row!=renamed.rows.end()&&renamed_row->display_name==std::optional<std::string>{"Renamed Miner"}&&renamed_row->tickets_reported==2,"newer signed report from authenticated alias renames only its historical operator");
 AddLabeledReport(f,5,Attribution("<malformed>"),{3,6});ledger=f.Ledger();auto malformed=f.Snapshot(ledger);const auto invalid_name=std::find_if(malformed.rows.begin(),malformed.rows.end(),[&](const auto& row){return row.operator_id==NativeAuditOperator(empty,script(5));});check(invalid_name!=malformed.rows.end()&&!invalid_name->display_name,"authenticated malformed public label stays absent rather than becoming a profile");
 f.Append({CTxOut{0,CScript{}<<OP_RETURN}},fork_parent);auto fork_ledger=f.Ledger();auto fork=f.Snapshot(fork_ledger);uint64_t fork_passed=0;Integer fork_volume{};for(const auto& row:fork.rows){fork_passed+=row.audits_finalized_valid;fork_volume+=row.validated_search_positions;}
 check(fork.tip!=snapshot.tip&&fork_passed==0&&fork_volume==0&&fork.rows.size()==2,"reorg removes orphan finalized audits and volume without double counting");
 check(fork.rows[0].display_name==std::optional<std::string>{"Shared Miner"}&&fork.rows[1].display_name==std::optional<std::string>{"Shared Miner"},"reorganization removes orphan rename and restores canonical disclosed label");
 canonical.SetTip(*f.tip);check(!cache.Get(&f,snapshot.tip,f.chain,canonical)&&!cache.Get(&f,repeated.tip,f.chain,canonical),"actual branch reorganization refuses retained orphan anchors");
 rejected=false;try{cache.Publish(old_snapshot,canonical);}catch(...){rejected=true;}check(rejected,"orphaned cold projection cannot publish after reorganization");
 check(snapshot.rows.size()==4&&NativeLeaderboardPage(snapshot,0,100)["rows"].size()==4,"prior immutable snapshot stays internally coherent");
 auto late=f.Bindings(fork_ledger);const auto old_id=LeaderboardOperatorAt(late,script(6),0);check(old_id==NativeAuditOperator(empty,script(6)),"pre-enrollment script identity preserved");
 uint256 replacement;replacement.begin()[0]=99;LeaderboardBindings temporal;const auto sixth=script(6);LeaderboardBind(temporal,Bytes{sixth.begin(),sixth.end()},replacement,10);
 check(LeaderboardOperatorAt(temporal,script(6),9)==old_id&&LeaderboardOperatorAt(temporal,script(6),10)==replacement,"later enrollment never reattributes prior report or job");
 // Pure metadata-selection tests (not a native-authentication claim): exercise
 // equal-height canonical tie order independent of ledger/map iteration order.
 const auto claim=[](const std::string& name){UniValue body{UniValue::VOBJ};body.pushKV("schema","PGN_NETWORK_RANGE_REPORT_V1");body.pushKV("discoverer",Attribution(name));const auto text=body.write();return std::pair{text,*uint256::FromHex(HexStr(AuditSha(Bytes{text.begin(),text.end()})))};};
 const auto claim_a=claim("Tie A"),claim_b=claim("Tie B"),too_long=claim(std::string(193,'a'));const auto id_a=*uint256::FromHex(std::string(60,'0')+"0100"),id_b=*uint256::FromHex(std::string(60,'0')+"0001");NativeLeaderboardRow tie_a,tie_b;
 ConsiderLeaderboardDisplayName(tie_a,42,id_a,claim_a.first,claim_a.second);ConsiderLeaderboardDisplayName(tie_a,42,id_b,claim_b.first,claim_b.second);ConsiderLeaderboardDisplayName(tie_b,42,id_b,claim_b.first,claim_b.second);ConsiderLeaderboardDisplayName(tie_b,42,id_a,claim_a.first,claim_a.second);
 check(tie_a.display_name==std::optional<std::string>{"Tie B"}&&tie_b.display_name==tie_a.display_name,"equal inclusion height chooses smallest canonical request id independent of iteration");
 ConsiderLeaderboardDisplayName(tie_a,43,id_a,too_long.first,too_long.second);check(tie_a.display_name==std::optional<std::string>{"Tie B"},"over-192-byte disclosure cannot overwrite prior valid public label");
 NativeLeaderboardRow wrong_root;ConsiderLeaderboardDisplayName(wrong_root,1,id_a,claim_a.first,claim_b.second);check(!wrong_root.display_name,"label whose disclosure does not match immutable root is not displayed");
 std::cout<<"CHECKS "<<checks<<std::endl;return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}}
