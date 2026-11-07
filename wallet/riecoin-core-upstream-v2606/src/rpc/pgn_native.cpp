// Copyright (c) 2026 The PGN developers. MIT.
// Native current-state work interface. No search, wallet keys or caller seals.
#include <pgn/native_node_options.h>
#include <pgn/rpc_audit_cache.h>
#include <pgn/rpc_audit_incremental.h>
#include <pgn/rpc_audit_commitment.h>
#include <pgn/rpc_history_cache.h>
#include <pgn/rpc_public_attribution.h>
#include <pgn/native_network_metrics.h>
#include <pgn/ticket_atlas_projection.h>
#include <pgn/proposed_ticket_requests.h>
#include <pgn/native_work_duty_admission.h>
#include <pgn/native_science_monetary_plan.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <node/blockstorage.h>
#include <node/miner.h>
#include <node/mining_args.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <util/time.h>
#include <util/strencodings.h>
#include <limits>
#include <logging.h>
#include <set>
#include <memory>
using namespace pgn::core;
namespace {
void RequireNative(const ChainstateManager& manager){
    if(!manager.GetConsensus().pgn_native_proof||!manager.m_options.pgn_candidate_science_budget||
       !manager.m_options.pgn_candidate_bootstrap_budget)throw JSONRPCError(RPC_MISC_ERROR,"Complete native PGN profile required");
}
struct OwnHistory {const NativeRequestHistoryArchive& archive;std::shared_ptr<const VerifiedRequestRegistry> registry;};
OwnHistory ReadOwnHistory(ChainstateManager& manager){
    AssertLockHeld(cs_main);
    RequireNative(manager);auto& active=manager.ActiveChainstate();const auto& b=manager.m_options.pgn_candidate_science_budget->schedule;
    const auto* parent=manager.ActiveChain().Tip();const auto* archive=active.PgnRequestHistory();
    if(!parent)throw JSONRPCError(RPC_MISC_ERROR,"Own native history unavailable");
    const auto genesis=manager.GetConsensus().hashGenesisBlock;const auto tip=parent->GetBlockHash();
    static RpcHistorySnapshotCache<VerifiedRequestRegistry,uint256> saved;
    const auto head=active.CoinsTip().GetPgnFactHead();
    if(!head||head->chain!=genesis||head->tip!=tip||active.CoinsTip().GetBestBlock()!=tip||!parent->IsValid(BLOCK_VALID_SCRIPTS)){
        saved.reset();throw JSONRPCError(RPC_MISC_ERROR,"Own native history does not match current parent");
    }
    if(auto hit=saved.get(&active,tip,genesis);archive&&hit&&hit->Size()<=b.maximum_requests)return {*archive,std::move(hit)};
    saved.reset(); // Release the prior branch before building another snapshot.
    std::optional<VerifiedRequestRegistry> registry;
    auto replay=active.ReplayPgnRequestHistory(registry,{b.maximum_blocks,b.maximum_requests,b.archive,b.scan});
    while(replay.verdict==RequestReplayVerdict::MoreWork&&replay.blocks)
        replay=active.ReplayPgnRequestHistory(registry,{b.maximum_blocks,b.maximum_requests,b.archive,b.scan});
    archive=active.PgnRequestHistory(); // Cold replay may have opened the archive.
    if(replay.verdict!=RequestReplayVerdict::AtParent||!registry||!archive)
        throw JSONRPCError(RPC_MISC_ERROR,"Own native history unavailable or local reconstruction capacity reached");
    auto snapshot=std::make_shared<const VerifiedRequestRegistry>(std::move(*registry));
    saved.retain(&active,tip,genesis,snapshot);
    return {*archive,std::move(snapshot)};
}
CBlockIndex& Tip(ChainstateManager& manager){auto* tip=manager.ActiveChain().Tip();if(!tip)throw JSONRPCError(RPC_IN_WARMUP,"Native chain tip unavailable");return *tip;}
std::string HexBlock(const CBlock& block){DataStream bytes;bytes<<TX_WITH_WITNESS(block);return HexStr(std::span<const unsigned char>{reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size()});}
std::string HexHeader(const CBlock& block){DataStream bytes;bytes<<static_cast<const CBlockHeader&>(block);return HexStr(std::span<const unsigned char>{reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size()});}
// RPC-only reuse of one immutable branch snapshot. Consensus verification
// always reconstructs its own state. cs_main protects both the anchor and
// this byte-bounded cache; a new tip, chainstate or reorg invalidates it immediately.
std::shared_ptr<const NativeAuditLedger> ReadRpcAuditLedger(ChainstateManager& manager,const OwnHistory& history,const CBlockIndex& tip){
 AssertLockHeld(cs_main);
 static RpcAuditSnapshotCache<NativeAuditLedger,uint256> saved;
 auto& active=manager.ActiveChainstate();const auto genesis=manager.GetConsensus().hashGenesisBlock;
 if(auto hit=saved.get(&active,tip.GetBlockHash(),genesis))return hit;
 const auto& budget=*manager.m_options.pgn_candidate_science_budget;
 if(tip.pprev){
  auto snapshot=saved.advance(&active,tip.pprev->GetBlockHash(),tip.GetBlockHash(),genesis,
   [&](const NativeAuditLedger& previous){return AdvanceRpcAuditLedger(previous,active.CoinsTip(),tip,history.archive,budget.policy,budget.schedule,budget.signature);});
  if(snapshot){LogDebug(BCLog::RPC,"PGN RPC audit snapshot direct-child %s\n",tip.GetBlockHash().GetHex());return snapshot;}
 }
 saved.reset(); // Release the old retained ledger before rebuilding another tip.
 auto ledger=ReadNativeAuditLedger(active.CoinsTip(),tip,history.archive,budget.policy,budget.schedule,budget.signature);
 if(!ledger)return {};
 auto snapshot=std::make_shared<const NativeAuditLedger>(std::move(*ledger));
 // Retain one immutable snapshot by charge budget, not an unrelated row
 // cutoff. Variable allocated capacities (including signatures) are charged.
 saved.retain(&active,tip.GetBlockHash(),genesis,snapshot);

 return snapshot;
}
RPCMethod getpgnstate(){
 return RPCMethod{"getpgnstate","Read actual native issuance, target and assigned ranges at the current tip.",{{"offset",RPCArg::Type::NUM,RPCArg::Default{0},"First lease in current branch order"},{"limit",RPCArg::Type::NUM,RPCArg::Default{100},"Page size 1 through 100"},{"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Exact owned request lookup"}},
 RPCResult{RPCResult::Type::OBJ,"","Native state",{
  {RPCResult::Type::STR_HEX,"genesis","Native genesis"},{RPCResult::Type::NUM,"height","Current height"},
  {RPCResult::Type::NUM,"median_time_past","Median chain time at this exact tip"},
  {RPCResult::Type::STR_HEX,"bestblockhash","Current tip"},{RPCResult::Type::STR,"phase","bootstrap or scientific"},
  {RPCResult::Type::STR,"cap_atomic","Exact lifetime cap"},{RPCResult::Type::STR,"minted_atomic","Exact cumulative issued amount"},
  {RPCResult::Type::STR,"promised_atomic","Exact unissued obligations"},{RPCResult::Type::STR,"near_miss_pool_atomic","Exact near miss pool"},
  {RPCResult::Type::STR,"audit_pool_atomic","Exact audit pool"},{RPCResult::Type::NUM,"merit_target_q16","Next scientific threshold /65536"},
   {RPCResult::Type::NUM,"issuance_snapshot_version","Retained native snapshot format; absence is preserved"},
   {RPCResult::Type::STR,"audit_paid_atomic",true,"Exact cumulative audit reserve paid; present only in v5"},
   {RPCResult::Type::STR_HEX,"audit_activation_contract",true,"Published compiled existing-chain activation contract digest"},
   {RPCResult::Type::NUM,"audit_activation_height",true,"Consensus activation height; does not select a local activation"},
  {RPCResult::Type::NUM,"segments","Report segmentation"},
  {RPCResult::Type::ARR,"leases","Actual active assignments",{{RPCResult::Type::OBJ,"","Lease",{
   {RPCResult::Type::STR_HEX,"request","Signed request ID"},{RPCResult::Type::STR,"start","Inclusive lower-prime bound"},
   {RPCResult::Type::STR,"end","Exclusive lower-prime bound"},{RPCResult::Type::NUM,"issued","Issue height"},
   {RPCResult::Type::STR,"deadline_clock","ledger_height or median_chain_time"},
   {RPCResult::Type::NUM,"deadline",true,"Legacy computation deadline height"},
   {RPCResult::Type::NUM,"issued_time",true,"Timed computation issue median chain time"},
   {RPCResult::Type::NUM,"deadline_time",true,"Timed computation deadline; submitted reports remain retained"},
   {RPCResult::Type::NUM,"lease_seconds",true,"Signed computation duration"},{RPCResult::Type::STR,"state","Assigned or submitted"},
   {RPCResult::Type::STR_HEX,"report_root",true,"Present after report commitment"},{RPCResult::Type::STR_HEX,"miner_script","Authenticated miner script"},
   {RPCResult::Type::STR_HEX,"reward_script","Authenticated reward script"}}}}}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& active=manager.ActiveChainstate();auto& tip=Tip(manager);
  const auto history=ReadOwnHistory(manager);const auto& budget=*manager.m_options.pgn_candidate_science_budget;
  const auto money=active.CoinsTip().GetPgnIssuanceState();if(!money||!money->pools||!money->partition)
    throw JSONRPCError(RPC_MISC_ERROR,"Native monetary state unavailable; no zero fallback");
  const auto range=ReadChainRangePolicy(active.CoinsTip(),tip,history.archive,budget.schedule.archive,budget.policy);
  const auto science=ReadChainSciencePolicy(active.CoinsTip(),tip,history.archive,budget.schedule.archive,budget.policy);
  if(!range.policy||!science.policy)throw JSONRPCError(RPC_MISC_ERROR,"Own native definitions unavailable");
   const auto offset=request.params[0].isNull()?0:request.params[0].getInt<int64_t>(),limit=request.params[1].isNull()?100:request.params[1].getInt<int64_t>();if(offset<0||limit<1||limit>100)throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded lease page required");
   std::optional<uint256> selected;if(!request.params[2].isNull()){const auto text=request.params[2].get_str();selected=uint256::FromHex(text);if(!selected||selected->IsNull()||selected->GetHex()!=text)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical request lookup required");}
   std::vector<CandidateRangeLease> leases_to_show;size_t total_active{};
   if(selected){
    auto found=ReconstructCandidateRangeLease(active.CoinsTip(),tip,*range.policy,history.archive,budget.schedule,*selected);
    if(found.verdict!=RangeScheduleVerdict::ReconstructedCandidate)throw JSONRPCError(RPC_MISC_ERROR,"Complete native range schedule held; no fabricated free ranges");
    total_active=found.active_count;if(found.lease)leases_to_show.push_back(std::move(*found.lease));
   }else{
    auto schedule=ReconstructCandidateRangeSchedule(active.CoinsTip(),tip,*range.policy,history.archive,budget.schedule);
    if(!schedule.schedule)throw JSONRPCError(RPC_MISC_ERROR,"Complete native range schedule held; no fabricated free ranges");
    total_active=schedule.schedule->active.size();leases_to_show=std::move(schedule.schedule->active);
   }
  const auto phase=InspectNativeBootstrapPhase(active.CoinsTip(),tip,manager.m_options.pgn_candidate_bootstrap_budget->maximum_phase_blocks);
  if(phase!=NativeBootstrapPhaseVerdict::Bootstrap&&phase!=NativeBootstrapPhaseVerdict::Scientific)throw JSONRPCError(RPC_MISC_ERROR,"Native phase history held");
  const auto q=DeriveHistoricalMeritTarget(tip,NativeParameterController(*manager.GetConsensus().pgn_native_proof));
  if(!q)throw JSONRPCError(RPC_MISC_ERROR,"Native target unavailable");
  UniValue result{UniValue::VOBJ};result.pushKV("genesis",manager.GetConsensus().hashGenesisBlock.GetHex());result.pushKV("height",tip.nHeight);
  result.pushKV("bestblockhash",tip.GetBlockHash().GetHex());result.pushKV("median_time_past",tip.GetMedianTimePast());result.pushKV("phase",phase==NativeBootstrapPhaseVerdict::Bootstrap?"bootstrap":"scientific");
  result.pushKV("cap_atomic",std::to_string(money->cap));result.pushKV("minted_atomic",std::to_string(money->minted));result.pushKV("promised_atomic",std::to_string(money->promised));
  result.pushKV("near_miss_pool_atomic",std::to_string(money->pools->near_miss));result.pushKV("audit_pool_atomic",std::to_string(money->pools->audit));
   result.pushKV("issuance_snapshot_version",money->audit_settlement?5:money->epochs?4:money->pools?3:money->partition?2:1);
   if(money->audit_settlement)result.pushKV("audit_paid_atomic",std::to_string(money->audit_settlement->total_paid));
   if(const auto activation=ConfiguredAuditSettlementActivation(manager.GetConsensus().hashGenesisBlock);activation&&activation->chain==manager.GetConsensus().hashGenesisBlock){
       result.pushKV("audit_activation_contract",activation->Id().GetHex());result.pushKV("audit_activation_height",activation->height);
   }
  if(money->epochs){result.pushKV("gold_blocks",money->epochs->golds);result.pushKV("last_gold_ledger_height",money->epochs->last_gold_height);}else if(phase==NativeBootstrapPhaseVerdict::Bootstrap){result.pushKV("gold_blocks",0);result.pushKV("last_gold_ledger_height",0);}else throw JSONRPCError(RPC_MISC_ERROR,"Authenticated Gold epoch counters unavailable");
  result.pushKV("merit_target_q16",uint64_t(q->target_q16));result.pushKV("segments",uint64_t(science.policy->Definition().segments));
  result.pushKV("range_origin",range.policy->Definition().origin.convert_to<std::string>());result.pushKV("ticket_width",range.policy->Definition().width.convert_to<std::string>());result.pushKV("range_limit",range.policy->Definition().limit.value_or(range.policy->Definition().origin+range.policy->Definition().width*range.policy->Definition().slots).convert_to<std::string>());result.pushKV("selection_window",range.policy->Definition().selection_window);
   result.pushKV("active_lease_count",uint64_t(total_active));result.pushKV("lease_offset",offset);int64_t index=0,taken=0;UniValue leases{UniValue::VARR};for(const auto& lease:leases_to_show){if(selected&&lease.owner.Id()!=*selected)continue;if(index++<offset)continue;if(taken++>=limit)break;
   UniValue item{UniValue::VOBJ};item.pushKV("request",lease.owner.Id().GetHex());item.pushKV("start",lease.assignment.start.convert_to<std::string>());
   item.pushKV("end",lease.assignment.end.convert_to<std::string>());item.pushKV("issued",lease.issued);
   if(lease.time_lease){
    item.pushKV("deadline_clock","median_chain_time");item.pushKV("issued_time",lease.time_lease->issued);
    item.pushKV("deadline_time",lease.time_lease->deadline);item.pushKV("lease_seconds",lease.owner.Request().lease_seconds);
   }else{item.pushKV("deadline_clock","ledger_height");item.pushKV("deadline",lease.deadline);}
   item.pushKV("state",lease.report_root?"submitted_pending_audits":"assigned");if(lease.report_root)item.pushKV("report_root",lease.report_root->GetHex());
   const Integer number=(lease.assignment.start-range.policy->Definition().origin)/range.policy->Definition().width+1;item.pushKV("ticket_number",number.convert_to<std::string>());
   item.pushKV("miner_script",HexStr(lease.owner.Request().miner));item.pushKV("reward_script",HexStr(lease.owner.Request().reward));leases.push_back(item);
  }result.pushKV("leases",leases);return result;
 }};
}
RPCMethod getpgnticketrequest(){
 return RPCMethod{"getpgnticketrequest","Create a native ticket challenge; an optional active-ancestor anchor preserves signing across concurrent tip updates.",{
  {"miner",RPCArg::Type::STR,RPCArg::Optional::NO,"Native signing address"},
  {"reward",RPCArg::Type::STR,RPCArg::Optional::OMITTED,"Native reward address; defaults to miner"},
  {"signature",RPCArg::Type::STR,RPCArg::Optional::OMITTED,"Canonical base64 BIP322 simple signature of returned challenge"},
  {"anchor",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Previously returned active-chain anchor; defaults to current tip"}},
 RPCResult{RPCResult::Type::OBJ,"","Public challenge/request",{
  {RPCResult::Type::STR,"schema","Ticket request RPC contract"},
  {RPCResult::Type::NUM,"request_body_version","Signed request body version"},
  {RPCResult::Type::STR,"deadline_clock","ledger_height or median_chain_time"},
  {RPCResult::Type::NUM,"lease_seconds","Zero for legacy height contracts; fixed duration for timed contracts"},
  {RPCResult::Type::STR_HEX,"genesis","Native genesis"},{RPCResult::Type::STR_HEX,"anchor","Signed active-chain anchor"},
  {RPCResult::Type::NUM,"anchor_height","Current parent height"},{RPCResult::Type::STR_HEX,"request_id","Exact signed request ID"},
  {RPCResult::Type::STR,"challenge","Message to sign with native wallet signmessage"},
  {RPCResult::Type::STR_HEX,"miner_script","Exact signing script"},{RPCResult::Type::STR_HEX,"reward_script","Exact signed reward script"},
  {RPCResult::Type::STR_HEX,"envelope",true,"Present only after actual signature verification; no inclusion/eligibility claim"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);
  const CBlockIndex* anchor=&tip;
  if(!request.params[3].isNull()){
   const auto text=request.params[3].get_str();const auto hash=uint256::FromHex(text);
   if(!hash||hash->IsNull()||hash->GetHex()!=text)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical active-chain anchor required");
   anchor=manager.m_blockman.LookupBlockIndex(*hash);
   if(!anchor||anchor->nHeight>tip.nHeight||tip.GetAncestor(anchor->nHeight)!=anchor||!anchor->IsValid(BLOCK_VALID_SCRIPTS))
    throw JSONRPCError(RPC_INVALID_PARAMETER,"Ticket anchor is not on the active verified chain");
  }
  const auto miner=DecodeDestination(request.params[0].get_str());
  const auto reward=request.params[1].isNull()?miner:DecodeDestination(request.params[1].get_str());
  if(!IsValidDestination(miner)||!IsValidDestination(reward))throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Valid native miner/reward addresses required");
  const auto& budget=manager.m_options.pgn_candidate_science_budget->schedule.scan.request;
  TicketRequest ticket{manager.GetConsensus().hashGenesisBlock,anchor->GetBlockHash(),anchor->nHeight,GetScriptForDestination(miner),GetScriptForDestination(reward)};
  if(budget.allow_time_leases)ticket.lease_seconds=NativeTimedTicketLeaseSeconds;
  const auto id=TicketRequestIdentity(ticket,budget);if(!id)throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid native request structure");
  const auto challenge=ScriptWorkChallenge({ticket.chain,*id,ticket.anchor},ticket.miner,{budget.miner_bytes,budget.signature_bytes});
  if(!challenge)throw JSONRPCError(RPC_INVALID_PARAMETER,"Native script is not supported for a ticket proof");
  UniValue result{UniValue::VOBJ};result.pushKV("schema","PGN_TICKET_REQUEST_RPC_V2");result.pushKV("request_body_version",ticket.lease_seconds?2:1);result.pushKV("deadline_clock",ticket.lease_seconds?"median_chain_time":"ledger_height");result.pushKV("lease_seconds",ticket.lease_seconds);result.pushKV("genesis",ticket.chain.GetHex());result.pushKV("anchor",ticket.anchor.GetHex());result.pushKV("anchor_height",ticket.anchor_height);
  result.pushKV("request_id",id->GetHex());result.pushKV("challenge",*challenge);result.pushKV("miner_script",HexStr(ticket.miner));result.pushKV("reward_script",HexStr(ticket.reward));
  if(!request.params[2].isNull()){
   const auto input=request.params[2].get_str();if(input.size()>4*((budget.signature_bytes+2)/3))throw JSONRPCError(RPC_INVALID_PARAMETER,"Signature exceeds native capacity");
   const auto signature=DecodeBase64(input);if(!signature||EncodeBase64(*signature)!=input)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical base64 signature required");
   const auto envelope=BuildTicketRequestEnvelope(ticket,*signature,budget);
   if(!envelope.envelope)throw JSONRPCError(RPC_VERIFY_ERROR,"Invalid native ticket signature or anchor; refresh the challenge");
   result.pushKV("envelope",HexStr(envelope.envelope->Script()));
  }return result;
 }};
}
RPCMethod getpgnbootstraptemplate(){
 return RPCMethod{"getpgnbootstraptemplate","Build native coordination work and any exact reserved audit page at the actual parent. No work acceptance, search, spends or caller-selected range. At most256 supplied signed request scripts.",{
  {"requests",RPCArg::Type::ARR,RPCArg::Optional::OMITTED,"Native signed request envelopes",{{"script",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Canonical request script bytes"}}}},
 RPCResult{RPCResult::Type::OBJ,"","Public native work",{
  {RPCResult::Type::STR,"schema","Template schema"},{RPCResult::Type::STR_HEX,"genesis","Native genesis"},
  {RPCResult::Type::STR_HEX,"parent","Exact parent"},{RPCResult::Type::NUM,"height","Proposed height"},
  {RPCResult::Type::STR_HEX,"header","Exactly112 serialized native bytes with counter0"},
  {RPCResult::Type::STR_HEX,"block","Whole proposed block, counter0; NOT accepted work"},
  {RPCResult::Type::STR_HEX,"numeric_target","256bit little-digest arithmetic target, printed big endian"},
  {RPCResult::Type::NUM,"nonce_offset","Counter offset in header bytes"},{RPCResult::Type::NUM,"nonce_bytes","Counter byte count"},
  {RPCResult::Type::BOOL,"work_verified","Alwaysfalse: submitblock must verify the actual work"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& active=manager.ActiveChainstate();auto& tip=Tip(manager);
  if(tip.nHeight==std::numeric_limits<int32_t>::max())throw JSONRPCError(RPC_MISC_ERROR,"Native height domain exhausted");
  const auto history=ReadOwnHistory(manager);const auto& budget=*manager.m_options.pgn_candidate_bootstrap_budget;
   const auto& science_budget=*manager.m_options.pgn_candidate_science_budget;
   const auto monetary=ReadChainMonetaryPolicy(active.CoinsTip(),tip,history.archive,science_budget.schedule.archive,science_budget.policy);
   if(!monetary.policy)throw JSONRPCError(RPC_MISC_ERROR,"Coordination monetary policy held");
   const bool continuation=budget.ledger_continuation||(monetary.policy->Audits()&&monetary.policy->Activation()&&monetary.policy->Activation()->coordination_after_gold);
  const auto ledger_phase=InspectNativeBootstrapPhase(active.CoinsTip(),tip,budget.maximum_phase_blocks);
   if(!NativeLedgerAllowsCoordination(ledger_phase,continuation))
    throw JSONRPCError(RPC_MISC_ERROR,"Native ledger phase is held or unavailable");
  CMutableTransaction cb;cb.vin.resize(1);cb.vin[0].prevout.SetNull();cb.vin[0].scriptSig=CScript{}<<(tip.nHeight+1)<<OP_0;
  size_t remaining=MAX_BLOCK_WEIGHT/WITNESS_SCALE_FACTOR;
  if(!request.params[0].isNull()){
   const auto& scripts=request.params[0].get_array();if(scripts.size()>256)throw JSONRPCError(RPC_INVALID_PARAMETER,"Template request count exceeds local capacity256");
   for(const auto& entry:scripts.getValues()){
    const auto text=entry.get_str();if(!IsHex(text)||text.size()>2*remaining)throw JSONRPCError(RPC_INVALID_PARAMETER,"Request scripts exceed canonical hex/body capacity");
    remaining-=text.size()/2;const auto bytes=ParseHex(text);cb.vout.emplace_back(0,CScript{bytes.begin(),bytes.end()});
   }
  }
  if(cb.vout.empty())cb.vout.emplace_back(0,CScript{}<<OP_RETURN<<Bytes{'P','G','N','B','O','O','T'});
  CBlock block;block.nVersion=4;block.hashPrevBlock=tip.GetBlockHash();block.nTime=std::max<int64_t>(GetTime(),tip.GetMedianTimePast()+1);
  block.nBits=manager.GetConsensus().pgn_native_proof->bootstrap_bits;block.nNonce=NativeBootstrapNonce(0);block.vtx={MakeTransactionRef(cb)};block.hashMerkleRoot=BlockMerkleRoot(block);
   const NativeAuditSettlementContext settlement_context{history.archive,science_budget.policy,science_budget.schedule,science_budget.signature};
   const auto settlement=PrepareNativeAuditCoordinationPlan(active.CoinsTip(),tip,block,*monetary.policy,settlement_context);
   if(!settlement)throw JSONRPCError(RPC_MISC_ERROR,"Coordination audit settlement held");
   cb.vout.insert(cb.vout.end(),settlement->page.outputs.begin(),settlement->page.outputs.end());
   if(settlement->page.receipt)cb.vout.emplace_back(0,*settlement->page.receipt);
    if(settlement->page.activation)cb.vout.emplace_back(0,*settlement->page.activation);
   block.vtx[0]=MakeTransactionRef(cb);block.hashMerkleRoot=BlockMerkleRoot(block);
   if(GetBlockWeight(block)>MAX_BLOCK_WEIGHT)throw JSONRPCError(RPC_INVALID_PARAMETER,"Native template exceeds actual block weight");
  const auto proposed=VerifyProposedTicketRequests(active.CoinsTip(),tip,*history.registry,block,budget.requests.scan,budget.requests.maximum_total_requests);
  if(!proposed.batch)throw JSONRPCError(RPC_VERIFY_ERROR,"Supplied request is invalid, duplicate, stale or already confirmed");
  const auto& science=*manager.m_options.pgn_candidate_science_budget;
  if(!VerifyProposedNativeWorkDuties(active.CoinsTip(),tip,history.archive,*proposed.batch,
       science.policy,science.schedule,science.signature))
    throw JSONRPCError(RPC_VERIFY_ERROR,"Native audit service is required before requesting another search range; refresh work duty status");
  if(!VerifyProposedNativeReportCommitments(active.CoinsTip(),tip,history.archive,block,science.policy,science.schedule,science.signature)||
     !VerifyProposedNativeAuditEvents(active.CoinsTip(),tip,history.archive,block,science.policy,science.schedule,science.signature))
    throw JSONRPCError(RPC_VERIFY_ERROR,"Report commitment is invalid, stale or conflicts with confirmed history");
  const auto target=CanonicalBootstrapHashTarget(block.nBits);if(!target)throw JSONRPCError(RPC_MISC_ERROR,"Native bootstrap target unavailable");
  UniValue result{UniValue::VOBJ};result.pushKV("schema","PGN_NATIVE112_BOOTSTRAP_TEMPLATE1");result.pushKV("genesis",manager.GetConsensus().hashGenesisBlock.GetHex());
  result.pushKV("parent",tip.GetBlockHash().GetHex());result.pushKV("height",tip.nHeight+1);result.pushKV("header",HexHeader(block));result.pushKV("block",HexBlock(block));
  result.pushKV("numeric_target",target->GetHex());result.pushKV("nonce_offset",82);result.pushKV("nonce_bytes",8);result.pushKV("work_verified",false);return result;
 }};
}

RPCMethod getpgnscientificwork(){
 return RPCMethod{"getpgnscientificwork","Prepare a public scientific challenge from own current history and policies. No caller-selected range or target; no admission or search.",{
  {"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::NO,"Confirmed request ID"},
  {"report_root",RPCArg::Type::STR_HEX,RPCArg::Optional::NO,"32 raw commitment bytes, canonical lowercase hex; zero may be used to inspect the contract"}},
 RPCResult{RPCResult::Type::OBJ,"","Actual parent-bound scientific work",{
  {RPCResult::Type::STR,"schema","Schema"},{RPCResult::Type::STR_HEX,"genesis","Native chain"},{RPCResult::Type::STR_HEX,"parent","Current parent"},
  {RPCResult::Type::NUM,"height","Candidate height"},{RPCResult::Type::STR_HEX,"request_id","Confirmed request"},{RPCResult::Type::STR_HEX,"ticket","Raw ticket bytes"},
  {RPCResult::Type::STR,"start","Inclusive owned lower-prime bound"},{RPCResult::Type::STR,"length","Exact owned width"},{RPCResult::Type::NUM,"segments","Report segments"},
  {RPCResult::Type::STR_HEX,"report_root","Raw report root"},{RPCResult::Type::STR_HEX,"work_id","Context identity"},{RPCResult::Type::STR,"challenge","BIP322 message to sign"},
  {RPCResult::Type::STR_HEX,"miner_script","Actual authenticated signing script"},{RPCResult::Type::STR_HEX,"reward_script","Actual authenticated payout script"},
  {RPCResult::Type::STR,"target_numerator","Exact merit numerator"},{RPCResult::Type::STR,"target_denominator","Exact merit denominator"},
  {RPCResult::Type::STR_HEX,"science_policy_script","Canonical complete current science policy for exact builder decoding"},
  {RPCResult::Type::OBJ,"native_audit_service","Confirmed request's native service accounting; no new audit assignment",{
   {RPCResult::Type::STR,"schema","Receipt schema"},{RPCResult::Type::BOOL,"enforced","Whether this confirmed request was subject to the native service gate"},
   {RPCResult::Type::STR_HEX,"request_id","Consuming search request"},{RPCResult::Type::STR_HEX,"view_parent","Authenticated observation tip"},
   {RPCResult::Type::STR_HEX,"issuance_block","Canonical block containing this request"},{RPCResult::Type::NUM,"issuance_height","Request inclusion height"},
   {RPCResult::Type::ARR,"credits","Zero through two one-use audit deliveries consumed by this request",{
    {RPCResult::Type::OBJ,"","Consumed audit credit",{
     {RPCResult::Type::STR_HEX,"request_id","Foreign report request"},{RPCResult::Type::NUM,"slot","Audit slot"},
     {RPCResult::Type::NUM,"attempt","Audit attempt"},{RPCResult::Type::STR_HEX,"commitment","Delivered answer commitment"}}}}}}},
  {RPCResult::Type::BOOL,"accepted","Always false; public work is not admission"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& active=manager.ActiveChainstate();auto& tip=Tip(manager);
  if(tip.nHeight==std::numeric_limits<int32_t>::max())throw JSONRPCError(RPC_MISC_ERROR,"Native height exhausted");
  const auto idText=request.params[0].get_str(),rootText=request.params[1].get_str();const auto id=uint256::FromHex(idText);
  if(!id||id->GetHex()!=idText||rootText.size()!=64||!IsHex(rootText)||HexStr(ParseHex(rootText))!=rootText)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical32byte request and report root required");
  Digest root{};const auto rootBytes=ParseHex(rootText);std::copy(rootBytes.begin(),rootBytes.end(),root.begin());
  const auto history=ReadOwnHistory(manager);const auto& budget=*manager.m_options.pgn_candidate_science_budget;
  const auto range=ReadChainRangePolicy(active.CoinsTip(),tip,history.archive,budget.schedule.archive,budget.policy);
  const auto science=ReadChainSciencePolicy(active.CoinsTip(),tip,history.archive,budget.schedule.archive,budget.policy);
  if(!range.policy||!science.policy)throw JSONRPCError(RPC_MISC_ERROR,"Own native policies held");
  const auto allocation=DeriveNativeCandidateReservation(active.CoinsTip(),tip,*range.policy,history.archive,budget.schedule,*id);
  if(!allocation.reservation)throw JSONRPCError(RPC_VERIFY_ERROR,"Request is not eligible at current parent");
  const auto context=NativeProfiledWorkContext(active.CoinsTip(),tip,*range.policy,*science.policy,*allocation.reservation,root);
  if(!context)throw JSONRPCError(RPC_MISC_ERROR,"Native science context held");const auto work=ScientificWorkIdentity(*context);
  const auto challenge=work?ScriptWorkChallenge({context->chain,*work,tip.GetBlockHash()},allocation.reservation->Lease().owner.Request().miner,budget.signature):std::nullopt;
  const auto policy=BuildCandidateSciencePolicyEnvelope(science.policy->Definition(),budget.policy);
  if(!work||!challenge||!policy)throw JSONRPCError(RPC_MISC_ERROR,"Native scientific challenge unavailable");
  const auto& owner=allocation.reservation->Lease().owner.Request();UniValue out{UniValue::VOBJ};out.pushKV("schema","PGN_NATIVE_SCIENTIFIC_WORK1");
  out.pushKV("genesis",context->chain.GetHex());out.pushKV("parent",tip.GetBlockHash().GetHex());out.pushKV("height",tip.nHeight+1);out.pushKV("request_id",idText);
  out.pushKV("ticket_number",((allocation.reservation->Lease().assignment.start-range.policy->Definition().origin)/range.policy->Definition().width+1).convert_to<std::string>());
  out.pushKV("ticket",HexStr(context->range.ticket_id));out.pushKV("start",context->range.start.convert_to<std::string>());out.pushKV("length",context->range.length.convert_to<std::string>());
  out.pushKV("segments",uint64_t(context->range.segments));out.pushKV("report_root",rootText);out.pushKV("work_id",work->GetHex());out.pushKV("challenge",*challenge);
  out.pushKV("miner_script",HexStr(owner.miner));out.pushKV("reward_script",HexStr(owner.reward));out.pushKV("target_numerator",context->policy.target_numerator.convert_to<std::string>());
  out.pushKV("target_denominator",context->policy.target_denominator.convert_to<std::string>());out.pushKV("science_policy_script",HexStr(*policy));out.pushKV("accepted",false);
  const auto confirmed=LookupConfirmedRequest(active.CoinsTip(),tip,*history.registry,*id);
  if(confirmed.verdict!=RequestRegistryVerdict::ReadyAtParent||!confirmed.location)throw JSONRPCError(RPC_MISC_ERROR,"Confirmed request location held");
  const auto* issued=tip.GetAncestor(confirmed.location->height);
  if(!issued||!issued->phashBlock||issued->GetBlockHash()!=confirmed.location->block||!issued->IsValid(BLOCK_VALID_SCRIPTS))
   throw JSONRPCError(RPC_MISC_ERROR,"Confirmed request inclusion held");
  const auto activation=ConfiguredNativeWorkDutyActivation(manager.GetConsensus().hashGenesisBlock);const bool enforced=activation&&confirmed.location->height>=activation->height;
  UniValue service{UniValue::VOBJ};service.pushKV("schema","PGN_NATIVE_ASSIGNED_AUDIT_SERVICE_V1");service.pushKV("enforced",enforced);
  service.pushKV("request_id",idText);service.pushKV("view_parent",tip.GetBlockHash().GetHex());
  service.pushKV("issuance_block",issued->GetBlockHash().GetHex());service.pushKV("issuance_height",confirmed.location->height);
  UniValue credits{UniValue::VARR};
  if(enforced){
   const auto duties=ReadNativeWorkDutyState(active.CoinsTip(),tip,history.archive,budget.policy,budget.schedule,budget.signature,*activation);
   if(!duties)throw JSONRPCError(RPC_MISC_ERROR,"Confirmed audit service accounting held");
   for(const auto& [ref,consumer]:duties->spent){
    if(consumer!=*id)continue;
    const auto found=duties->audits.find(ref.report);
    if(credits.size()>=2||found==duties->audits.end()||ref.slot>1)throw JSONRPCError(RPC_MISC_ERROR,"Confirmed audit service reference held");
    const auto& job=found->second.jobs[ref.slot];
    if(!job||job->actor!=owner.miner||job->attempt!=ref.attempt||job->commitment!=ref.commitment)
     throw JSONRPCError(RPC_MISC_ERROR,"Confirmed audit service delivery held");
    UniValue row{UniValue::VOBJ};row.pushKV("request_id",ref.report.GetHex());row.pushKV("slot",uint64_t(ref.slot));
    row.pushKV("attempt",uint64_t(ref.attempt));row.pushKV("commitment",ref.commitment);credits.push_back(row);
   }
  }
  service.pushKV("credits",credits);out.pushKV("native_audit_service",service);return out;
 }};
}
RPCMethod getpgnscientifictemplate(){
 return RPCMethod{"getpgnscientifictemplate","Reverify supplied scientific proof at the actual parent and construct its exact native payout. No search or acceptance; submitblock remains authoritative.",{
  {"proof_script",RPCArg::Type::STR_HEX,RPCArg::Optional::NO,"Canonical native science proof script"}},
 RPCResult{RPCResult::Type::OBJ,"","Verified candidate only",{
  {RPCResult::Type::STR,"schema","Schema"},{RPCResult::Type::STR_HEX,"parent","Current parent"},{RPCResult::Type::NUM,"height","Candidate height"},
  {RPCResult::Type::STR_HEX,"hash","Candidate hash"},{RPCResult::Type::STR_HEX,"block","Whole native block"},{RPCResult::Type::STR,"winner_atomic","Exact native payout including configured fee share"},
   {RPCResult::Type::STR,"subsidy_atomic","Native Gold winner subsidy; fees are not issuance"},{RPCResult::Type::STR,"fees_atomic","Exact selected transaction fees"},{RPCResult::Type::STR,"winner_fee_atomic","Configured native share of transaction fees"},
  {RPCResult::Type::NUM,"merit_target_q16","Actual next threshold"},{RPCResult::Type::BOOL,"work_verified","Whole candidate scientific mathematics and payout checked"},
  {RPCResult::Type::BOOL,"accepted","Always false; only actual native submission can accept"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& active=manager.ActiveChainstate();auto& tip=Tip(manager);
  if(tip.nHeight==std::numeric_limits<int32_t>::max())throw JSONRPCError(RPC_MISC_ERROR,"Native height exhausted");const auto& budget=*manager.m_options.pgn_candidate_science_budget;
  const auto text=request.params[0].get_str();if(text.empty()||text.size()>2*budget.maximum_script_bytes||!IsHex(text))throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded canonical proof hex required");
  const auto bytes=ParseHex(text);if(HexStr(bytes)!=text)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical proof hex required");
  const auto history=ReadOwnHistory(manager);const auto money=ReadChainMonetaryPolicy(active.CoinsTip(),tip,history.archive,budget.schedule.archive,budget.policy);
  const auto target=DeriveHistoricalMeritTarget(tip,NativeParameterController(*manager.GetConsensus().pgn_native_proof));
  if(!money.policy||!target)throw JSONRPCError(RPC_MISC_ERROR,"Actual monetary policy or target held");
  CMutableTransaction cb;cb.vin.resize(1);cb.vin[0].prevout.SetNull();cb.vin[0].scriptSig=CScript{}<<(tip.nHeight+1)<<OP_0;cb.vout.emplace_back(0,CScript{bytes.begin(),bytes.end()});
  CBlock block;block.nVersion=4;block.hashPrevBlock=tip.GetBlockHash();block.nTime=std::max<int64_t>(GetTime(),tip.GetMedianTimePast()+1);block.nBits=target->target_q16;block.nNonce=0;
  block.vtx={MakeTransactionRef(cb)};block.hashMerkleRoot=BlockMerkleRoot(block);
  if(GetBlockWeight(block)>MAX_BLOCK_WEIGHT)throw JSONRPCError(RPC_INVALID_PARAMETER,"Proof exceeds block weight");
  const auto checked=VerifyNativeCandidateBlockScience(active.CoinsTip(),tip,history.archive,block,budget);
  if(!checked.candidate)throw JSONRPCError(RPC_VERIFY_ERROR,"Scientific proof rejected at current parent");
  const NativeAuditSettlementContext audit_context{history.archive,budget.policy,budget.schedule,budget.signature};
  const auto quote=PrepareNativeScienceMonetaryPlan(active.CoinsTip(),tip,block,*checked.candidate,*money.policy,&audit_context);
  if(!quote)throw JSONRPCError(RPC_VERIFY_ERROR,"Native monetary plan held");const auto winner_index=cb.vout.size();cb.vout.emplace_back(quote->WinnerSubsidy(),quote->Recipient());
  cb.vout.insert(cb.vout.end(),quote->Audit().outputs.begin(),quote->Audit().outputs.end());
  if(quote->Audit().receipt)cb.vout.emplace_back(0,*quote->Audit().receipt);
  block.vtx[0]=MakeTransactionRef(cb);
  // Generate the maintained Core commitment before selection, so the reserved
  // native coinbase weight includes its actual witness and commitment output.
  manager.GenerateCoinbaseCommitment(block,&tip);
  const auto options=node::ReadMiningArgs(EnsureAnyArgsman(request.context));
  if(!options)throw JSONRPCError(RPC_MISC_ERROR,"Local block assembly options invalid");
  auto selected=node::BlockAssembler{active,&EnsureAnyMemPool(request.context),*options}.SelectTransactionsForBlock(std::move(block));
  CAmount actual_fees{};
  for(const CAmount fee:selected->vTxFees){
   if(!manager.GetConsensus().amount_rules||!manager.GetConsensus().amount_rules->CanAdd(actual_fees,fee))throw JSONRPCError(RPC_VERIFY_ERROR,"Selected transaction fee domain invalid");
   actual_fees+=fee;
  }
  const auto fee_share=QuoteNativeWinnerFees(actual_fees,money.policy->Definition());
  if(!fee_share||*fee_share>PGN_MAX_SUPPLY_ATOMIC-quote->WinnerSubsidy())throw JSONRPCError(RPC_VERIFY_ERROR,"Native transaction fee policy held");
  block=std::move(selected->block);CMutableTransaction final_cb{*block.vtx.front()};
  final_cb.vout.at(winner_index).nValue=quote->WinnerSubsidy()+*fee_share;
  block.vtx.front()=MakeTransactionRef(std::move(final_cb));
  // The seed already has a Core commitment, satisfying this helper's
  // precondition. Rebuild witness/Merkle commitments after body/payout changes.
  node::RegenerateCommitments(block,manager);
  block.fChecked=false;block.checked_amount_max.reset();block.m_checked_merkle_root=false;block.m_checked_witness_commitment=false;
  if(GetBlockWeight(block)>MAX_BLOCK_WEIGHT)throw JSONRPCError(RPC_INVALID_PARAMETER,"Final candidate exceeds block weight");
  const auto finalCheck=VerifyNativeCandidateBlockScience(active.CoinsTip(),tip,history.archive,block,budget);
  const auto finalQuote=finalCheck.candidate?PrepareNativeScienceMonetaryPlan(active.CoinsTip(),tip,block,*finalCheck.candidate,*money.policy,&audit_context):std::nullopt;
  if(!finalQuote||!VerifyNativeScienceCoinbase(*finalQuote,block,actual_fees))throw JSONRPCError(RPC_VERIFY_ERROR,"Final native candidate verification failed");
  // Use the maintained native ConnectBlock path in a disposable child: actual
  // UTXOs, maturity, locks, scripts, witness and fees authorize no DB mutation.
  const auto final_hash=block.GetHash();CBlockIndex proposed{block};proposed.phashBlock=&final_hash;
  proposed.pprev=&tip;proposed.nHeight=tip.nHeight+1;proposed.nTx=block.vtx.size();proposed.nStatus=BLOCK_VALID_TRANSACTIONS;proposed.BuildSkip();
  CCoinsViewCache child{&active.CoinsTip()};BlockValidationState state;
  if(!active.ConnectScientificCandidateBlock(block,state,&proposed,child,*finalCheck.candidate,true))throw JSONRPCError(RPC_VERIFY_ERROR,"Final native body rejected: "+state.ToString());
  UniValue out{UniValue::VOBJ};out.pushKV("schema","PGN_NATIVE_SCIENTIFIC_TEMPLATE1");out.pushKV("parent",tip.GetBlockHash().GetHex());out.pushKV("height",tip.nHeight+1);
  out.pushKV("hash",block.GetHash().GetHex());out.pushKV("block",HexBlock(block));out.pushKV("winner_atomic",std::to_string(finalQuote->WinnerSubsidy()+*fee_share));out.pushKV("subsidy_atomic",std::to_string(finalQuote->WinnerSubsidy()));out.pushKV("fees_atomic",std::to_string(actual_fees));out.pushKV("winner_fee_atomic",std::to_string(*fee_share));out.pushKV("merit_target_q16",uint64_t(block.nBits));out.pushKV("work_verified",true);out.pushKV("accepted",false);return out;
 }};
}

RPCMethod getpgnreportcommit(){
 return RPCMethod{"getpgnreportcommit","Create an owner-signed immutable report commitment at the current parent. This grants no audit acceptance or coins.",{
  {"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::NO,"Confirmed automatically assigned request"},
  {"report_root",RPCArg::Type::STR_HEX,RPCArg::Optional::NO,"Canonical report commitment hash"},
  {"signature",RPCArg::Type::STR,RPCArg::Optional::OMITTED,"Canonical base64 BIP322 signature of returned challenge"}},
 RPCResult{RPCResult::Type::OBJ,"","Public report commitment",{
  {RPCResult::Type::STR_HEX,"genesis","Native genesis"},{RPCResult::Type::STR_HEX,"parent","Exact current parent"},
  {RPCResult::Type::STR_HEX,"request_id","Confirmed request"},{RPCResult::Type::STR_HEX,"ticket","Derived ticket"},
  {RPCResult::Type::STR_HEX,"report_root","Committed report hash"},{RPCResult::Type::STR_HEX,"report_id","Stable commitment identity"},
  {RPCResult::Type::STR,"challenge","BIP322 message to sign"},{RPCResult::Type::STR_HEX,"envelope",true,"Present only after owner verification"},
  {RPCResult::Type::BOOL,"accepted","Always false: neither inclusion nor audits have occurred"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& active=manager.ActiveChainstate();auto& tip=Tip(manager);
  const auto request_text=request.params[0].get_str(),root_text=request.params[1].get_str();
  const auto id=uint256::FromHex(request_text),root=uint256::FromHex(root_text);
  if(!id||id->IsNull()||id->GetHex()!=request_text||!root||root->IsNull()||root->GetHex()!=root_text)
    throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical nonzero request and report hashes required");
  const auto history=ReadOwnHistory(manager);const auto& b=*manager.m_options.pgn_candidate_science_budget;
  const auto policy=ReadChainRangePolicy(active.CoinsTip(),tip,history.archive,b.schedule.archive,b.policy);
  const auto reservation=policy.policy?DeriveNativeCandidateReservation(active.CoinsTip(),tip,*policy.policy,history.archive,b.schedule,*id):CandidateReservationCheck{CandidateReservationVerdict::HistoryHeld,{},{}};
  if(!reservation.reservation)throw JSONRPCError(RPC_INVALID_PARAMETER,"Request has no current authenticated automatic ticket");
  const auto previous=ReadConfirmedNativeReports(active.CoinsTip(),tip,history.archive,b.schedule);
  if(!previous)throw JSONRPCError(RPC_MISC_ERROR,"Own report history held");
  const auto old=previous->find(*id);
  if(old!=previous->end()&&old->second.commitment.root!=*root)throw JSONRPCError(RPC_VERIFY_ERROR,"An immutable report is already committed for this attempt");
  NativeReportCommitment report{*id,reservation.reservation->Ticket(),*root,tip.GetBlockHash(),{}};
  const auto identity=NativeReportWorkIdentity(policy.policy->Chain(),report);const auto& owner=reservation.reservation->Lease().owner.Request().miner;
  const auto challenge=ScriptWorkChallenge({policy.policy->Chain(),identity,report.parent},owner,b.signature);
  if(!challenge)throw JSONRPCError(RPC_MISC_ERROR,"Report signing challenge unavailable");
  UniValue out{UniValue::VOBJ};out.pushKV("genesis",policy.policy->Chain().GetHex());out.pushKV("parent",report.parent.GetHex());out.pushKV("request_id",id->GetHex());
  out.pushKV("ticket",HexStr(std::span<const unsigned char>{report.ticket.begin(),32}));out.pushKV("report_root",root->GetHex());out.pushKV("report_id",identity.GetHex());out.pushKV("challenge",*challenge);out.pushKV("accepted",false);
  if(!request.params[2].isNull()){
   const auto input=request.params[2].get_str();
   if(input.size()>4*((b.signature.signature_bytes+2)/3))throw JSONRPCError(RPC_INVALID_PARAMETER,"Signature exceeds capacity");
   const auto signature=DecodeBase64(input);
   if(!signature||EncodeBase64(*signature)!=input||!VerifyScriptWork({policy.policy->Chain(),identity,report.parent},owner,*signature,b.signature).proof)
    throw JSONRPCError(RPC_VERIFY_ERROR,"Report signature does not match its ticket owner at this parent");
   report.signature=*signature;const auto envelope=BuildNativeReportEnvelope(report);
   if(!envelope)throw JSONRPCError(RPC_INVALID_PARAMETER,"Report envelope exceeds canonical bounds");out.pushKV("envelope",HexStr(*envelope));
  }return out;
 }};
}
RPCMethod getpgnreports(){
 return RPCMethod{"getpgnreports","Read committed report roots from authenticated current-branch history. Expected audit counts and report salts are not published.",{{"offset",RPCArg::Type::NUM,RPCArg::Default{0},"First report in current branch order"},{"limit",RPCArg::Type::NUM,RPCArg::Default{100},"Page size, 1 through 100"},{"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Exact report request lookup"}},
 RPCResult{RPCResult::Type::ARR,"","Confirmed report commitments",{{RPCResult::Type::OBJ,"","Report",{
  {RPCResult::Type::STR_HEX,"request_id","Owner request"},{RPCResult::Type::STR_HEX,"ticket","Assigned ticket"},
  {RPCResult::Type::STR_HEX,"report_root","Immutable report commitment"},{RPCResult::Type::STR_HEX,"block","Confirming block"},{RPCResult::Type::STR_HEX,"miner_script","Authenticated report owner"},
  {RPCResult::Type::NUM,"height","Confirmation height"},{RPCResult::Type::STR,"state","Pending independent audits"},
  {RPCResult::Type::BOOL,"scientific_accepted","False until independent evidence is verified"}}}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);const auto history=ReadOwnHistory(manager);
  const auto reports=ReadConfirmedNativeReports(manager.ActiveChainstate().CoinsTip(),tip,history.archive,manager.m_options.pgn_candidate_science_budget->schedule);
  if(!reports)throw JSONRPCError(RPC_MISC_ERROR,"Complete own report history unavailable");
  const auto offset=request.params[0].isNull()?0:request.params[0].getInt<int64_t>(),limit=request.params[1].isNull()?100:request.params[1].getInt<int64_t>();if(offset<0||limit<1||limit>100)throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded report page required");
  std::optional<uint256> selected;if(!request.params[2].isNull()){const auto text=request.params[2].get_str();selected=uint256::FromHex(text);if(!selected||selected->IsNull()||selected->GetHex()!=text)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical report lookup required");}
  int64_t index=0,taken=0;UniValue result{UniValue::VARR};for(const auto& [id,r]:*reports){if(selected&&id!=*selected)continue;if(index++<offset)continue;if(taken++>=limit)break;
   const auto location=LookupConfirmedRequest(manager.ActiveChainstate().CoinsTip(),tip,*history.registry,id);
   if(!location.location)throw JSONRPCError(RPC_MISC_ERROR,"Confirmed report owner history unavailable");
   const auto owner_block=history.archive.Load(manager.ActiveChainstate().CoinsTip(),tip,location.location->height,manager.m_options.pgn_candidate_science_budget->schedule.archive);
   if(!owner_block.block||location.location->output>=owner_block.block->Coinbase()->vout.size())throw JSONRPCError(RPC_MISC_ERROR,"Report owner body unavailable");
   const auto owner=DecodeTicketRequestEnvelope(owner_block.block->Coinbase()->vout[location.location->output].scriptPubKey,manager.m_options.pgn_candidate_science_budget->schedule.scan.request);
   if(!owner.envelope||owner.envelope->Request().Id()!=id)throw JSONRPCError(RPC_MISC_ERROR,"Report owner authentication failed");
   UniValue row{UniValue::VOBJ};row.pushKV("request_id",id.GetHex());row.pushKV("ticket",HexStr(std::span<const unsigned char>{r.commitment.ticket.begin(),32}));row.pushKV("report_root",r.commitment.root.GetHex());
   row.pushKV("block",r.block.GetHex());row.pushKV("miner_script",HexStr(owner.envelope->Request().Request().miner));row.pushKV("height",r.height);row.pushKV("state","pending_independent_audits");row.pushKV("scientific_accepted",false);result.push_back(row);
  }return result;
 }};
}

RPCMethod getpgnauditevent(){
 return RPCMethod{"getpgnauditevent","Prepare a signed audit event. Assignment is automatic; answers remain hidden until both auditors commit. Nothing is accepted by this RPC.",{
  {"action",RPCArg::Type::STR,RPCArg::Optional::NO,"assign, commit, reveal, reveal_report, or enroll"},
  {"miner",RPCArg::Type::STR,RPCArg::Optional::NO,"Native signing address"},
  {"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Assigned report request; omitted for automatic assignment"},
  {"slot",RPCArg::Type::NUM,RPCArg::Optional::OMITTED,"Assigned audit slot"},
  {"attempt",RPCArg::Type::NUM,RPCArg::Optional::OMITTED,"Assigned attempt"},
  {"body",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Answer commitment or canonical reveal bytes"},
  {"signature",RPCArg::Type::STR,RPCArg::Optional::OMITTED,"Base64 BIP322 signature of returned challenge"}},
 RPCResult{RPCResult::Type::OBJ,"","Audit event challenge",{
  {RPCResult::Type::BOOL,"available","False if no independent work is available"},
  {RPCResult::Type::STR_HEX,"genesis","Native genesis"},{RPCResult::Type::STR_HEX,"parent","Current parent for both available and unavailable results"},{RPCResult::Type::STR_HEX,"request_id",true,"Selected report"},
  {RPCResult::Type::NUM,"slot",true,"Selected slot"},{RPCResult::Type::NUM,"attempt",true,"Current attempt"},
  {RPCResult::Type::STR,"challenge",true,"Message to sign"},{RPCResult::Type::STR_HEX,"envelope",true,"Owner-verified event"},
  {RPCResult::Type::ARR,"ranges",true,"Five automatically assigned ranges",{{RPCResult::Type::OBJ,"","Partition",{
   {RPCResult::Type::NUM,"index","Partition index"},{RPCResult::Type::STR,"start","Inclusive lower bound"},{RPCResult::Type::STR,"end","Exclusive lower bound"}}}}},
  {RPCResult::Type::BOOL,"accepted","Always false until block confirmation"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);const auto history=ReadOwnHistory(manager);
  const auto& b=*manager.m_options.pgn_candidate_science_budget;auto ledger=ReadRpcAuditLedger(manager,history,tip);
  if(!ledger)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated audit history held");
  const auto destination=DecodeDestination(request.params[1].get_str());if(!IsValidDestination(destination))throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Native signing address required");
  NativeAuditEvent event;event.parent=tip.GetBlockHash();event.actor=GetScriptForDestination(destination);const auto action=request.params[0].get_str();
  if(action=="assign")event.action=NativeAuditAction::Assign;else if(action=="commit")event.action=NativeAuditAction::Commit;else if(action=="reveal")event.action=NativeAuditAction::Reveal;else if(action=="reveal_report")event.action=NativeAuditAction::ReportReveal;else if(action=="enroll")event.action=NativeAuditAction::Enroll;else throw JSONRPCError(RPC_INVALID_PARAMETER,"Unknown audit action");
  UniValue out{UniValue::VOBJ};out.pushKV("accepted",false);
  out.pushKV("genesis",manager.GetConsensus().hashGenesisBlock.GetHex());out.pushKV("parent",tip.GetBlockHash().GetHex());
  if(event.action==NativeAuditAction::Assign){
   for(size_t i=2;i<6;++i)if(!request.params[i].isNull())throw JSONRPCError(RPC_INVALID_PARAMETER,"Audit assignment cannot select its report, partition or attempt");
   const auto member=ledger->participants.find(Bytes{event.actor.begin(),event.actor.end()});
   if(member==ledger->participants.end()){out.pushKV("available",false);out.pushKV("unavailable_reason","auditor_not_registered");out.pushKV("reason","Audit registration required");return out;}
   if(!member->second.may_audit){out.pushKV("available",false);out.pushKV("unavailable_reason","audit_permission_disabled");out.pushKV("reason","Audit permission is not enabled");return out;}
   const auto selected=SelectNativeAudit(*ledger,event.actor,tip.nHeight,tip.GetMedianTimePast());
   if(!selected){out.pushKV("available",false);out.pushKV("unavailable_reason","no_eligible_independent_work");out.pushKV("reason","Waiting for eligible work from another operator");return out;}
   event.request=selected->first;event.slot=selected->second;const auto& previous=ledger->at(event.request).jobs[event.slot];event.attempt=previous?previous->attempt+1:0;
   event.body=NativeTimedAuditAssignmentBody();
  }else{
   if(request.params[2].isNull()||request.params[3].isNull()||request.params[4].isNull()||request.params[5].isNull())throw JSONRPCError(RPC_INVALID_PARAMETER,"Assigned request, slot, attempt and body required");
   const auto text=request.params[2].get_str();const auto id=uint256::FromHex(text);const auto slot=request.params[3].getInt<int64_t>(),attempt=request.params[4].getInt<int64_t>();
   if(!id||id->IsNull()||id->GetHex()!=text||slot<0||slot>1||attempt<0||attempt>std::numeric_limits<uint32_t>::max())throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid audit identity");
   event.request=*id;event.slot=slot;event.attempt=attempt;const auto encoded=request.params[5].get_str();if(encoded.size()>131072||!IsHex(encoded)||HexStr(ParseHex(encoded))!=encoded)throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded canonical body hex required");event.body=ParseHex(encoded);
   const auto found=ledger->find(event.request);
   if(event.action==NativeAuditAction::Enroll){if(event.request!=ledger->authority_request||event.actor!=ledger->authority)throw JSONRPCError(RPC_VERIFY_ERROR,"Participant admission requires the testnet authority");}
   else if(found==ledger->end())throw JSONRPCError(RPC_INVALID_PARAMETER,"Unknown confirmed report");
   else if(event.action==NativeAuditAction::ReportReveal){if(found->second.owner!=event.actor)throw JSONRPCError(RPC_VERIFY_ERROR,"Report belongs to another signer");}
   else {const auto& job=found->second.jobs[event.slot];if(!job||job->attempt!=event.attempt||job->actor!=event.actor)throw JSONRPCError(RPC_VERIFY_ERROR,"Audit belongs to another signer or attempt");}
  }
  const auto report_root=event.action==NativeAuditAction::Enroll?uint256{}:ledger->at(event.request).report.commitment.root;const auto identity=NativeAuditWorkIdentity(manager.GetConsensus().hashGenesisBlock,report_root,event);
  const auto challenge=ScriptWorkChallenge({manager.GetConsensus().hashGenesisBlock,identity,event.parent},event.actor,b.signature);if(!challenge)throw JSONRPCError(RPC_MISC_ERROR,"Audit signing challenge unavailable");
  out.pushKV("available",true);out.pushKV("request_id",event.request.GetHex());out.pushKV("slot",uint64_t(event.slot));out.pushKV("attempt",uint64_t(event.attempt));out.pushKV("challenge",*challenge);
  if(event.action==NativeAuditAction::Assign){
   const auto& report=ledger->at(event.request);const auto* seed=tip.GetAncestor(report.report.height+1);if(!seed)throw JSONRPCError(RPC_MISC_ERROR,"Post-report seed unavailable");
   const auto partitions=NativeAuditPartitions(report.report.commitment.root,seed->GetBlockHash());UniValue ranges{UniValue::VARR};
   for(auto index:partitions[event.slot]){UniValue row{UniValue::VOBJ};row.pushKV("index",uint64_t(index));row.pushKV("start",(report.range.start+report.range.length*index/100).convert_to<std::string>());row.pushKV("end",(report.range.start+report.range.length*(index+1)/100).convert_to<std::string>());ranges.push_back(row);}out.pushKV("ranges",ranges);out.pushKV("deadline_clock","median_chain_time");out.pushKV("issued_time",tip.GetMedianTimePast());out.pushKV("deadline_time",tip.GetMedianTimePast()+NativeAuditLeaseSeconds);out.pushKV("lease_seconds",NativeAuditLeaseSeconds);
  }
  if(!request.params[6].isNull()){
   const auto text=request.params[6].get_str();if(text.size()>4*((b.signature.signature_bytes+2)/3))throw JSONRPCError(RPC_INVALID_PARAMETER,"Audit signature exceeds bounds");const auto signature=DecodeBase64(text);if(!signature||EncodeBase64(*signature)!=text)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical audit signature required");event.signature=*signature;
   try{auto proposed=*ledger;ApplyNativeAuditEvent(proposed,event,tip,manager.GetConsensus().hashGenesisBlock,b.signature);}catch(const std::exception& error){throw JSONRPCError(RPC_VERIFY_ERROR,error.what());}
   const auto envelope=BuildNativeAuditEnvelope(event);if(!envelope)throw JSONRPCError(RPC_VERIFY_ERROR,"Audit event exceeds wire bounds");out.pushKV("envelope",HexStr(*envelope));
  }return out;
 }};
}
RPCMethod getpgnparticipants(){
 return RPCMethod{"getpgnparticipants","Read public testnet operator membership from authenticated native history. Signing-address aliases share one operator identifier; this does not prove human independence.",{
  {"offset",RPCArg::Type::NUM,RPCArg::Default{0},"First participant in signing-script order"},
  {"limit",RPCArg::Type::NUM,RPCArg::Default{100},"Page size, 1 through 100"},
  {"signing_address",RPCArg::Type::STR,RPCArg::Optional::OMITTED,"Exact native signing-address lookup"}},
 RPCResult{RPCResult::Type::OBJ,"","Public membership at one native tip",{
  {RPCResult::Type::STR_HEX,"genesis","Native chain"},{RPCResult::Type::STR_HEX,"bestblockhash","Exact accepted parent"},
  {RPCResult::Type::NUM,"height","Ledger height"},{RPCResult::Type::NUM,"total","Total admitted signing identities"},
  {RPCResult::Type::STR_HEX,"authority_script",true,"Testnet authority signing script"},
  {RPCResult::Type::STR_HEX,"authority_request",true,"Authority admission anchor"},
  {RPCResult::Type::ARR,"participants","Public memberships",{{RPCResult::Type::OBJ,"","Participant",{
   {RPCResult::Type::STR_HEX,"signing_script","Signing script"},{RPCResult::Type::STR_HEX,"operator_id","Stable operator identity"},
   {RPCResult::Type::BOOL,"may_audit","Audit permission"}}}}}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);
  const auto offset=request.params[0].isNull()?0:request.params[0].getInt<int64_t>();
  const auto limit=request.params[1].isNull()?100:request.params[1].getInt<int64_t>();
  if(offset<0||limit<1||limit>100)throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded participant page required");
  std::optional<Bytes> selected;
  if(!request.params[2].isNull()){
   const auto address=DecodeDestination(request.params[2].get_str());
   if(!IsValidDestination(address))throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Native signing address required");
   const auto script=GetScriptForDestination(address);selected=Bytes{script.begin(),script.end()};
  }
  const auto history=ReadOwnHistory(manager);const auto ledger=ReadRpcAuditLedger(manager,history,tip);
  if(!ledger)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated participant history held");
  UniValue out{UniValue::VOBJ};out.pushKV("genesis",manager.GetConsensus().hashGenesisBlock.GetHex());
  out.pushKV("bestblockhash",tip.GetBlockHash().GetHex());out.pushKV("height",tip.nHeight);
  out.pushKV("total",uint64_t(ledger->participants.size()));
  if(!ledger->authority.empty()){
   out.pushKV("authority_script",HexStr(ledger->authority));out.pushKV("authority_request",ledger->authority_request.GetHex());
  }
  UniValue rows{UniValue::VARR};int64_t skipped{};
  auto add=[&](const Bytes& script,const NativeAuditMembership& member){
   if(selected&&script!=*selected)return;if(skipped++<offset||rows.size()>=uint64_t(limit))return;
   UniValue row{UniValue::VOBJ};row.pushKV("signing_script",HexStr(script));row.pushKV("operator_id",member.operator_id.GetHex());
   row.pushKV("may_audit",member.may_audit);rows.push_back(row);
  };
  if(selected){const auto found=ledger->participants.find(*selected);if(found!=ledger->participants.end())add(found->first,found->second);}
  else for(const auto& [script,member]:ledger->participants){if(rows.size()>=uint64_t(limit))break;add(script,member);}
  out.pushKV("participants",rows);return out;
 }};
}

RPCMethod getpgnworkdutystatus(){
 return RPCMethod{"getpgnworkdutystatus","Read the native two-audit service gate at one authenticated tip. This read neither assigns work nor activates consensus.",{
  {"signing_address",RPCArg::Type::STR,RPCArg::Optional::NO,"Native mining signing address"}},
 RPCResult{RPCResult::Type::OBJ,"","Bounded native work-duty status",{
  {RPCResult::Type::STR,"schema","Status schema"},{RPCResult::Type::STR_HEX,"genesis","Native chain"},
  {RPCResult::Type::STR_HEX,"bestblockhash","Exact accepted parent"},{RPCResult::Type::NUM,"height","Ledger height"},
  {RPCResult::Type::BOOL,"enforced","Whether the next request is governed by this activation"},
  {RPCResult::Type::STR,"status","Gate outcome or not_activated"},
  {RPCResult::Type::BOOL,"may_request",true,"Present only when native service accounting is active"},
  {RPCResult::Type::NUM,"activation_height",true,"Configured activation height"},
  {RPCResult::Type::NUM,"spent_credits",true,"Canonical one-use audit credits already consumed"},
  {RPCResult::Type::ARR,"credits",true,"At most two deterministically selected public audit deliveries",{
   {RPCResult::Type::OBJ,"","Audit credit",{
    {RPCResult::Type::STR_HEX,"request_id","Audited report request"},{RPCResult::Type::NUM,"slot","Audit slot"},
    {RPCResult::Type::NUM,"attempt","Current native attempt"},{RPCResult::Type::STR_HEX,"commitment","Public answer commitment"}}}}}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);
  const auto destination=DecodeDestination(request.params[0].get_str());
  if(!IsValidDestination(destination))throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,"Native signing address required");
  const auto actor=GetScriptForDestination(destination);const auto activation=ConfiguredNativeWorkDutyActivation(manager.GetConsensus().hashGenesisBlock);
  UniValue out{UniValue::VOBJ};out.pushKV("schema","PGN_NATIVE_WORK_DUTY_STATUS_V1");
  out.pushKV("genesis",manager.GetConsensus().hashGenesisBlock.GetHex());out.pushKV("bestblockhash",tip.GetBlockHash().GetHex());out.pushKV("height",tip.nHeight);
  const bool enforced=activation&&int64_t(tip.nHeight)+1>=activation->height;out.pushKV("enforced",enforced);
  if(!enforced){out.pushKV("status","not_activated");return out;}
  const auto history=ReadOwnHistory(manager);const auto& b=*manager.m_options.pgn_candidate_science_budget;
  const auto state=ReadNativeWorkDutyState(manager.ActiveChainstate().CoinsTip(),tip,history.archive,b.policy,b.schedule,b.signature,*activation);
  if(!state)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated work-duty history held");
  const auto selected=InspectAutomaticNativeWorkDuty(state->audits,tip,actor,state->spent,activation->first_assignment_height);
  std::string status;
  switch(selected.checked.verdict){
   case NativeDutyVerdict::Ready:status="ready";break;
   case NativeDutyVerdict::NotEligible:status="audit_permission_required";break;
   case NativeDutyVerdict::AuditRequired:status="audit_work_required";break;
   case NativeDutyVerdict::MissingClaim:status="audit_reference_required";break;
   case NativeDutyVerdict::AlreadySpent:status="audit_already_used";break;
   default:status="invalid_context";break;
  }
  out.pushKV("status",status);out.pushKV("may_request",selected.checked.proof.has_value());
  out.pushKV("activation_height",activation->height);out.pushKV("spent_credits",uint64_t(state->spent.size()));
  UniValue credits{UniValue::VARR};
  for(const auto& ref:selected.references){UniValue row{UniValue::VOBJ};row.pushKV("request_id",ref.report.GetHex());row.pushKV("slot",uint64_t(ref.slot));row.pushKV("attempt",uint64_t(ref.attempt));row.pushKV("commitment",ref.commitment);credits.push_back(row);}
  out.pushKV("credits",credits);return out;
 }};
}

RPCMethod getpgnauditstate(){
 return RPCMethod{"getpgnauditstate","Read authenticated audit progress; audited sampling is not by itself an exact gap certificate or monetary payment.",{{"offset",RPCArg::Type::NUM,RPCArg::Default{0},"First report in current branch order"},{"limit",RPCArg::Type::NUM,RPCArg::Default{100},"Page size, 1 through 100"},{"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Exact report request lookup"},{"request_ids",RPCArg::Type::ARR,RPCArg::Optional::OMITTED,"Batch of 1 through 100 exact report requests; cannot combine with request_id",{{"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::NO,"Canonical report request"}}}},
 RPCResult{RPCResult::Type::ARR,"","Audit progress",{{RPCResult::Type::OBJ,"","Report",{
  {RPCResult::Type::STR_HEX,"request_id","Owner request"},{RPCResult::Type::BOOL,"audited","Both sampled audits agree"},
  {RPCResult::Type::STR_HEX,"details_root",true,"Merkle root of committed larger gaps after report disclosure"},
  {RPCResult::Type::OBJ,"public_attribution",true,"Bounded self-declared label from the root-verified disclosed owner report; not a unique identity",{
   {RPCResult::Type::STR,"schema","PGN_PUBLIC_ATTRIBUTION_V1"},{RPCResult::Type::STR,"name","Public label; clients enforce their display policy"}}},
  {RPCResult::Type::ARR,"jobs","Assigned tasks",{{RPCResult::Type::OBJ,"","Audit",{
   {RPCResult::Type::NUM,"slot","Slot"},{RPCResult::Type::NUM,"attempt","Attempt"},{RPCResult::Type::STR_HEX,"actor","Signing script"},
   {RPCResult::Type::STR_HEX,"commitment",true,"Public auditor result digest after commitment; no counts or nonce"},
   {RPCResult::Type::NUM,"deadline",true,"Legacy commitment deadline height"},{RPCResult::Type::STR,"deadline_clock","ledger_height or median_chain_time"},{RPCResult::Type::NUM,"issued_time",true,"Assignment chain time"},{RPCResult::Type::NUM,"deadline_time",true,"Commitment deadline chain time"},{RPCResult::Type::BOOL,"expired","Whether an uncommitted assignment can be reassigned"},{RPCResult::Type::STR,"state","Assigned, committed, passed or failed"}}}}}}}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);const auto history=ReadOwnHistory(manager);
  const auto ledger=ReadRpcAuditLedger(manager,history,tip);if(!ledger)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated audit history held");
  const auto offset=request.params[0].isNull()?0:request.params[0].getInt<int64_t>(),limit=request.params[1].isNull()?100:request.params[1].getInt<int64_t>();if(offset<0||limit<1||limit>100)throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded audit page required");
  std::optional<uint256> selected;if(!request.params[2].isNull()){const auto text=request.params[2].get_str();selected=uint256::FromHex(text);if(!selected||selected->IsNull()||selected->GetHex()!=text)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical report lookup required");}
  std::optional<std::set<uint256>> selected_batch;
  if(!request.params[3].isNull()){
   if(selected)throw JSONRPCError(RPC_INVALID_PARAMETER,"Choose a single request or a batch, not both");
   const auto& inputs=request.params[3].get_array();if(inputs.empty()||inputs.size()>100)throw JSONRPCError(RPC_INVALID_PARAMETER,"Audit batch must contain 1 through 100 requests");
   selected_batch.emplace();for(const auto& entry:inputs.getValues()){
    if(!entry.isStr())throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical report lookup required");
    const auto text=entry.get_str();const auto id=uint256::FromHex(text);
    if(!id||id->IsNull()||id->GetHex()!=text||!selected_batch->insert(*id).second)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical distinct report requests required");
   }
  }
  int64_t index=0,taken=0;UniValue out{UniValue::VARR};for(const auto& [id,report]:*ledger){if(selected&&id!=*selected)continue;if(selected_batch&&!selected_batch->contains(id))continue;if(index++<offset)continue;if(taken++>=limit)break;UniValue row{UniValue::VOBJ};row.pushKV("request_id",id.GetHex());row.pushKV("audited",report.Audited()&&NativeAuditOperatorsIndependent(*ledger,report));if(const auto label=DisclosedPublicAttribution(report.owner_reveal,report.report.commitment.root.GetHex(),[](std::string_view raw){return HexStr(AuditSha(Bytes{raw.begin(),raw.end()}));}))row.pushKV("public_attribution",*label);if(report.details_root)row.pushKV("details_root",HexStr(*report.details_root));UniValue jobs{UniValue::VARR};
   for(size_t slot=0;slot<2;++slot){if(!report.jobs[slot])continue;const auto& job=*report.jobs[slot];UniValue item{UniValue::VOBJ};item.pushKV("slot",uint64_t(slot));item.pushKV("attempt",uint64_t(job.attempt));item.pushKV("actor",HexStr(job.actor));if(const auto commitment=RpcAuditCommitment(job))item.pushKV("commitment",std::string(*commitment));if(job.time_lease){item.pushKV("deadline_clock","median_chain_time");item.pushKV("issued_time",job.time_lease->issued);item.pushKV("deadline_time",job.time_lease->deadline);}else{item.pushKV("deadline_clock","ledger_height");item.pushKV("deadline",job.deadline);}item.pushKV("expired",job.stage==NativeAuditJob::Assigned&&NativeAuditAssignmentExpired(job,tip.nHeight,tip.GetMedianTimePast()));item.pushKV("state",job.stage==NativeAuditJob::Assigned?"assigned":job.stage==NativeAuditJob::Committed?"committed":job.stage==NativeAuditJob::Passed?"passed":"failed");item.pushKV("issued",job.issued);UniValue ranges{UniValue::VARR};if(const auto* seed=tip.GetAncestor(report.report.height+1)){const auto partitions=NativeAuditPartitions(report.report.commitment.root,seed->GetBlockHash());for(auto part:partitions[slot]){UniValue range{UniValue::VOBJ};range.pushKV("index",uint64_t(part));range.pushKV("start",(report.range.start+report.range.length*part/100).convert_to<std::string>());range.pushKV("end",(report.range.start+report.range.length*(part+1)/100).convert_to<std::string>());ranges.push_back(range);}}item.pushKV("ranges",ranges);jobs.push_back(item);}row.pushKV("jobs",jobs);out.push_back(row);
  }return out;
 }};
}

RPCMethod getpgnworkview(){
 return RPCMethod{"getpgnworkview","Read a bounded coherent public work projection under one native chain lock. No expected audit answers, report salts or wallet data.",{
  {"kind",RPCArg::Type::STR,RPCArg::Optional::NO,"tickets or audits"},
  {"offset",RPCArg::Type::NUM,RPCArg::Default{0},"First row in current branch order"},
  {"limit",RPCArg::Type::NUM,RPCArg::Default{100},"Page size, 1 through 100"},
  {"request_id",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Exact request lookup"}},
 RPCResult{RPCResult::Type::OBJ_DYN,"","Bounded PGN_WORK_VIEW_V1 snapshot",{{RPCResult::Type::ELISION,"","Authenticated state, reports and matching public audit jobs at one tip"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  const auto kind=request.params[0].get_str();if(kind!="tickets"&&kind!="audits")throw JSONRPCError(RPC_INVALID_PARAMETER,"Choose tickets or audits");
  const auto offset=request.params[1].isNull()?0:request.params[1].getInt<int64_t>(),limit=request.params[2].isNull()?100:request.params[2].getInt<int64_t>();
  if(offset<0||limit<1||limit>100)throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded work page required");
  const bool exact=!request.params[3].isNull();if(exact){const auto text=request.params[3].get_str();const auto id=uint256::FromHex(text);if(!id||id->IsNull()||id->GetHex()!=text||offset!=0||limit!=1)throw JSONRPCError(RPC_INVALID_PARAMETER,"Exact lookup requires a canonical nonzero request, offset0 and limit1");}
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);
  // Existing authenticated projections are reused inside this single recursive
  // chain lock. They cannot observe different accepted tips between components.
  const auto invoke=[&](const char* method,const RPCMethod& handler,UniValue params){auto nested=request;nested.strMethod=method;nested.params=std::move(params);return handler.HandleRequest(nested);};
  UniValue parameters{UniValue::VARR};parameters.push_back(kind=="tickets"?offset:0);parameters.push_back(kind=="tickets"?limit:1);parameters.push_back(request.params[3]);
  const auto state=invoke("getpgnstate",getpgnstate(),parameters);UniValue reports{UniValue::VARR},audits{UniValue::VARR};
  if(kind=="audits"||exact){
   parameters=UniValue{UniValue::VARR};parameters.push_back(offset);parameters.push_back(limit);parameters.push_back(request.params[3]);reports=invoke("getpgnreports",getpgnreports(),parameters);
   if(!reports.empty()){
    parameters=UniValue{UniValue::VARR};parameters.push_back(0);parameters.push_back(limit);parameters.push_back(UniValue{});
    UniValue ids{UniValue::VARR};for(const auto& report:reports.getValues())ids.push_back(report["request_id"]);parameters.push_back(ids);
    audits=invoke("getpgnauditstate",getpgnauditstate(),parameters);
   }
  }
  UniValue out{UniValue::VOBJ};out.pushKV("schema","PGN_WORK_VIEW_V1");out.pushKV("kind",kind);out.pushKV("offset",offset);out.pushKV("limit",limit);if(exact)out.pushKV("request_id",request.params[3]);out.pushKV("state",state);out.pushKV("reports",reports);out.pushKV("audits",audits);
  if(out.write().size()>524288)throw JSONRPCError(RPC_MISC_ERROR,"Work projection exceeds response budget; choose a smaller page");return out;
 }};
}

RPCMethod getpgnticketatlas(){
 return RPCMethod{"getpgnticketatlas","Read exact bounded ticket-atlas bins and up to100 details at one verified current tip. Empty space is not completed coverage.",{
  {"start",RPCArg::Type::STR,RPCArg::Optional::OMITTED,"Inclusive integer viewport bound; omit both bounds for the full policy domain"},
  {"end",RPCArg::Type::STR,RPCArg::Optional::OMITTED,"Exclusive integer viewport bound"},
  {"bins",RPCArg::Type::NUM,RPCArg::Default{256},"Maximum display bins,1 through512"}},
  RPCResult{RPCResult::Type::OBJ_DYN,"","PGN_TICKET_ATLAS_V1",{{RPCResult::Type::ELISION,"","One chain anchor,exact domain,non-overlapping bins,counts and bounded public details"}}},RPCExamples{""},
  [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
   auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& active=manager.ActiveChainstate();auto& tip=Tip(manager);
   const auto history=ReadOwnHistory(manager);const auto& budget=*manager.m_options.pgn_candidate_science_budget;
   const auto policy=ReadChainRangePolicy(active.CoinsTip(),tip,history.archive,budget.schedule.archive,budget.policy);
   if(!policy.policy)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated range policy held");
   const auto& definition=policy.policy->Definition();const Integer origin=definition.origin;
   const Integer limit=definition.limit.value_or(origin+definition.width*definition.slots);
   const auto integer=[](const UniValue& value)->Integer{
    const auto text=value.get_str();if(text.empty()||text.size()>160||(text.size()>1&&text.front()=='0')||
     !std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';}))
      throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical unsigned decimal viewport required");
    return Integer(text);
   };
   if(request.params[0].isNull()!=request.params[1].isNull())throw JSONRPCError(RPC_INVALID_PARAMETER,"Supply both atlas bounds or neither");
   const Integer start=request.params[0].isNull()?origin:integer(request.params[0]),end=request.params[1].isNull()?limit:integer(request.params[1]);
   const auto bins=request.params[2].isNull()?256:request.params[2].getInt<int64_t>();
   if(start<origin||end>limit||start>=end||bins<1||bins>512)throw JSONRPCError(RPC_INVALID_PARAMETER,"Atlas viewport outside policy or display budget");
   const auto ledger=ReadRpcAuditLedger(manager,history,tip);if(!ledger)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated audit state held");
   TicketAtlasProjection projection(start,end,size_t(bins));UniValue details{UniValue::VARR};uint64_t visible{};
   std::optional<Integer> observed_start,observed_end;
   const auto complete=VisitCandidateRangeSchedule(active.CoinsTip(),tip,*policy.policy,history.archive,budget.schedule,[&](const CandidateRangeLease& lease){
    AtlasStage stage=AtlasStage::Assigned;const auto found=ledger->find(lease.owner.Id());
    if(lease.report_root){
     if(found==ledger->end()||found->second.report.commitment.root!=*lease.report_root)throw JSONRPCError(RPC_MISC_ERROR,"Atlas report anchor mismatch");
     const auto& report=found->second;stage=AtlasStage::Pending;
     if(report.Audited()&&NativeAuditOperatorsIndependent(*ledger,report))stage=AtlasStage::Passed;
     else for(const auto& slot:report.jobs)if(slot){
      if(slot->stage==NativeAuditJob::Failed){stage=AtlasStage::Failed;break;}
      if(slot->stage!=NativeAuditJob::Assigned||!NativeAuditAssignmentExpired(*slot,tip.nHeight,tip.GetMedianTimePast()))stage=AtlasStage::Checking;
     }
    }
    if(!observed_start)observed_start=lease.assignment.start;observed_end=lease.assignment.end;
    if(!projection.Add(lease.assignment.start,lease.assignment.end,stage))return;
    ++visible;if(details.size()>=100)return;
    UniValue row{UniValue::VOBJ};row.pushKV("request",lease.owner.Id().GetHex());
    row.pushKV("start",lease.assignment.start.convert_to<std::string>());row.pushKV("end",lease.assignment.end.convert_to<std::string>());
    const Integer number=(lease.assignment.start-origin)/definition.width+1;row.pushKV("ticket_number",number.convert_to<std::string>());
    row.pushKV("stage",AtlasStageNames[size_t(stage)]);row.pushKV("miner_script",HexStr(lease.owner.Request().miner));
    if(found!=ledger->end())if(const auto label=DisclosedPublicAttribution(found->second.owner_reveal,found->second.report.commitment.root.GetHex(),[](std::string_view raw){return HexStr(AuditSha(Bytes{raw.begin(),raw.end()}));}))row.pushKV("public_attribution",*label);
    details.push_back(row);
   });
   if(!complete.schedule)throw JSONRPCError(RPC_MISC_ERROR,"Complete authenticated atlas history held");
   const auto amounts=[](const std::array<AtlasAmount,5>& stages){UniValue out{UniValue::VOBJ};for(size_t s=0;s<stages.size();++s){UniValue value{UniValue::VOBJ};value.pushKV("tickets",stages[s].tickets);value.pushKV("positions",stages[s].positions.convert_to<std::string>());out.pushKV(AtlasStageNames[s],value);}return out;};
   UniValue out{UniValue::VOBJ};out.pushKV("schema","PGN_TICKET_ATLAS_V1");out.pushKV("genesis",manager.GetConsensus().hashGenesisBlock.GetHex());out.pushKV("bestblockhash",tip.GetBlockHash().GetHex());out.pushKV("height",tip.nHeight);
   out.pushKV("range_origin",origin.convert_to<std::string>());out.pushKV("range_limit",limit.convert_to<std::string>());out.pushKV("ticket_width",definition.width.convert_to<std::string>());
   out.pushKV("start",start.convert_to<std::string>());out.pushKV("end",end.convert_to<std::string>());out.pushKV("tracked_assignments",projection.AllAssignments());out.pushKV("visible_assignments",visible);
   out.pushKV("current_assignments_complete",true);out.pushKV("details_truncated",visible>details.size());out.pushKV("details",details);out.pushKV("totals",amounts(projection.Totals()));
   if(observed_start){out.pushKV("observed_start",observed_start->convert_to<std::string>());out.pushKV("observed_end",observed_end->convert_to<std::string>());}
   UniValue values{UniValue::VARR};for(const auto& bin:projection.Bins()){UniValue value{UniValue::VOBJ};value.pushKV("start",bin.start.convert_to<std::string>());value.pushKV("end",bin.end.convert_to<std::string>());value.pushKV("stages",amounts(bin.stages));values.push_back(value);}out.pushKV("bins",values);
   if(out.write().size()>524288)throw JSONRPCError(RPC_MISC_ERROR,"Atlas byte budget exceeded; request fewer bins");return out;
  }};
}
RPCMethod getpgnnetworkmetrics(){
 return RPCMethod{"getpgnnetworkmetrics","Read bounded chain-derived merit, reported search throughput and durable audit queue statistics. Reported throughput is not proof of audited work or online hardware.",{
  {"maximum_points",RPCArg::Type::NUM,RPCArg::Default{96},"Sample budget, 2 through 256"},
  {"span_seconds",RPCArg::Type::NUM,RPCArg::Default{86400},"Visible history, 60 through 604800 seconds"},
  {"window_seconds",RPCArg::Type::NUM,RPCArg::Default{600},"Throughput window, 30 through 86400 chain seconds"}},
 RPCResult{RPCResult::Type::OBJ_DYN,"","Bounded PGN_NETWORK_METRICS_V1 snapshot",{{RPCResult::Type::ELISION,"","Public metrics; no report answers or wallet data"}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  auto& manager=EnsureAnyChainman(request.context);LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);
  const auto points=request.params[0].isNull()?96:request.params[0].getInt<int64_t>();
  const auto span=request.params[1].isNull()?86400:request.params[1].getInt<int64_t>();
  const auto window=request.params[2].isNull()?600:request.params[2].getInt<int64_t>();
  if(points<2||points>256||span<60||span>604800||window<30||window>86400)throw JSONRPCError(RPC_INVALID_PARAMETER,"Bounded metrics window required");
  struct Saved {const Chainstate* chainstate{};uint256 tip,genesis;int64_t points{},span{},window{};UniValue value;};
  static Saved saved;auto& active=manager.ActiveChainstate();const auto genesis=manager.GetConsensus().hashGenesisBlock;
  if(saved.chainstate==&active&&saved.genesis==genesis&&saved.tip==tip.GetBlockHash()&&saved.points==points&&saved.span==span&&saved.window==window)return saved.value;
  const auto history=ReadOwnHistory(manager);const auto ledger=ReadRpcAuditLedger(manager,history,tip);
  if(!ledger)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated network history held");
  try{
    const auto& budget=*manager.m_options.pgn_candidate_science_budget;
    const auto range=ReadChainRangePolicy(active.CoinsTip(),tip,history.archive,budget.schedule.archive,budget.policy);
    if(!range.policy)throw std::runtime_error("Authenticated range domain unavailable");
    auto value=NativeNetworkMetrics(*ledger,tip,NativeParameterController(*manager.GetConsensus().pgn_native_proof),unsigned(points),span,window,&range.policy->Definition());
   saved={&active,tip.GetBlockHash(),genesis,points,span,window,value};return value;
  }catch(const std::exception& error){throw JSONRPCError(RPC_MISC_ERROR,error.what());}
 }};
}

}
void RegisterPgnNativeRPCCommands(CRPCTable& table){
 static const CRPCCommand commands[]{{"mining",&getpgnstate},{"mining",&getpgnticketrequest},{"mining",&getpgnbootstraptemplate},{"mining",&getpgnscientificwork},{"mining",&getpgnscientifictemplate},{"mining",&getpgnreportcommit},{"mining",&getpgnreports},{"mining",&getpgnauditevent},{"mining",&getpgnauditstate},{"mining",&getpgnparticipants},{"mining",&getpgnworkdutystatus},{"mining",&getpgnnetworkmetrics},{"mining",&getpgnworkview},{"mining",&getpgnticketatlas}};
 for(const auto& command:commands)table.appendCommand(command.name,&command);
}
