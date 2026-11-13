// Included in pgn_native.cpp after its authenticated snapshot helpers, inside
// the existing anonymous namespace. No consensus caller or new global service.
std::shared_ptr<const NativeLeaderboardSnapshot> ReadLeaderboardSnapshot(ChainstateManager& manager,const std::optional<uint256>& expected,LeaderboardUTCWindow window={},bool allow_build=true){
 static LeaderboardSnapshotCache cache;
 const auto checkExpected=[&](const CBlockIndex& tip){
  if(expected&&*expected!=tip.GetBlockHash())throw JSONRPCError(RPC_INVALID_PARAMETER,"Leaderboard snapshot reset: anchor is reorganized or no longer retained; restart pagination");
 };
 {
  LOCK(cs_main);RequireNative(manager);auto& tip=Tip(manager);
  if(auto hit=cache.Get(&manager.ActiveChainstate(),expected?*expected:tip.GetBlockHash(),manager.GetConsensus().hashGenesisBlock,manager.ActiveChain(),window))return hit;
  if(!allow_build)throw JSONRPCError(RPC_INVALID_PARAMETER,"Leaderboard snapshot reset: UTC window is no longer retained; start Today again");
  checkExpected(tip);
 }
 // Single flight for this read-only projection; never wait for this mutex
 // while holding cs_main. Other native RPC/mining paths do not acquire it.
 std::lock_guard single_builder(cache.build_mutex);
 std::shared_ptr<const NativeAuditLedger> ledger;
 std::shared_ptr<const NativeLeaderboardSnapshot> previous;
 const CBlockIndex* anchor{};const void* state{};uint256 genesis;
 const NativeRequestHistoryArchive* archive{};RequestHistoryArchiveBudget archive_budget{};
 std::unique_ptr<LeaderboardHistoryAnchorView> history_view;
 {
  LOCK(cs_main);RequireNative(manager);anchor=&Tip(manager);
  state=&manager.ActiveChainstate();genesis=manager.GetConsensus().hashGenesisBlock;
  if(auto hit=cache.Get(state,expected?*expected:anchor->GetBlockHash(),genesis,manager.ActiveChain(),window))return hit;
  if(!allow_build)throw JSONRPCError(RPC_INVALID_PARAMETER,"Leaderboard snapshot reset: UTC window is no longer retained; start Today again");
  checkExpected(*anchor);
  const auto history=ReadOwnHistory(manager);archive=&history.archive;
  ledger=ReadRpcAuditLedger(manager,history,*anchor);
  if(!ledger)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated leaderboard history unavailable");
  archive_budget=manager.m_options.pgn_candidate_science_budget->schedule.archive;
  history_view=LeaderboardHistoryAnchorView::Capture(manager.ActiveChainstate().CoinsTip(),*anchor);
  previous=cache.BeginBuild(state,genesis,manager.ActiveChain());
 }
 LeaderboardBindings bindings=previous?previous->bindings:LeaderboardBindings{};
 if(!previous&&!ledger->authority.empty()){
  NativeAuditLedger empty;
  LeaderboardBind(bindings,Bytes{ledger->authority.begin(),ledger->authority.end()},NativeAuditOperator(empty,ledger->authority),ledger->authority_height);
 }
 const int32_t first=previous?previous->height+1:1;
 // Copy at most sixteen authenticated immutable coinbases while locked;
 // release cs_main before decoding enrollment JSON or allocating projection
 // maps. A cold/reorg rebuild scans once, direct children scan only the child.
 // No page/UI poll rescans a cached tip. Every chunk rechecks the same anchor.
 for(int64_t begin=first;begin<=anchor->nHeight;begin+=16){
  std::vector<VerifiedNativeRequestBlock> blocks;
  {
   LOCK(cs_main);
   const auto* canonical=manager.ActiveChain()[anchor->nHeight];
   if(state!=&manager.ActiveChainstate()||manager.GetConsensus().hashGenesisBlock!=genesis||!canonical||canonical->GetBlockHash()!=anchor->GetBlockHash())
    throw JSONRPCError(RPC_INVALID_PARAMETER,"Leaderboard snapshot reset: anchor was reorganized during projection");
   for(int64_t height=begin;height<=anchor->nHeight&&height<begin+16;++height){
    const auto loaded=archive->Load(*history_view,*anchor,int32_t(height),archive_budget);
    if(!loaded.block)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated leaderboard membership history unavailable");
    blocks.push_back(*loaded.block);
   }
  }
  for(const auto& block:blocks)for(const auto& output:block.Coinbase()->vout){
   if(!IsNativeAuditScript(output.scriptPubKey))continue;
   const auto event=DecodeNativeAuditEnvelope(output.scriptPubKey);
   if(!event)throw JSONRPCError(RPC_MISC_ERROR,"Authenticated leaderboard event unavailable");
   if(event->action!=NativeAuditAction::Enroll)continue;
   UniValue body;if(!body.read(std::string{event->body.begin(),event->body.end()}))throw JSONRPCError(RPC_MISC_ERROR,"Leaderboard binding invalid");
   const auto script=ParseHex(body["signing_script"].get_str());
   const auto id=uint256::FromHex(body["operator_id"].get_str());
   const auto member=ledger->participants.find(script);
   // ReadRpcAuditLedger has already verified the native authority signatures
   // and event sequence. Match these exact authenticated branch bytes to it.
   if(!id||member==ledger->participants.end()||member->second.operator_id!=*id)throw JSONRPCError(RPC_MISC_ERROR,"Leaderboard binding provenance mismatch");
   LeaderboardBind(bindings,script,*id,block.Height());
  }
 }
 std::shared_ptr<const NativeLeaderboardSnapshot> snapshot;
 try{snapshot=std::make_shared<const NativeLeaderboardSnapshot>(BuildNativeLeaderboard(*ledger,*anchor,genesis,state,std::move(bindings),window));}
 catch(const std::exception& error){throw JSONRPCError(RPC_MISC_ERROR,error.what());}
 {
  LOCK(cs_main);
  if(state!=&manager.ActiveChainstate()||!LeaderboardAnchorCanonical(*snapshot,manager.ActiveChain()))throw JSONRPCError(RPC_INVALID_PARAMETER,"Leaderboard snapshot reset: anchor was reorganized before publication");
  cache.Publish(snapshot,manager.ActiveChain());
 }
 return snapshot;
}
RPCMethod getpgnleaderboard(){
 return RPCMethod{"getpgnleaderboard","Read exact productive-operator statistics from authenticated native history at one tip. No worker/uptime/presence inference.",{
  {"offset",RPCArg::Type::NUM,RPCArg::Default{0},"First ranked operator"},
  {"limit",RPCArg::Type::NUM,RPCArg::Default{100},"Page size 1 through 100"},
  {"bestblockhash",RPCArg::Type::STR_HEX,RPCArg::Optional::OMITTED,"Retained canonical snapshot anchor for coherent continuation; reorganization or eviction requires restart"},
  {"period",RPCArg::Type::STR,RPCArg::Default{"total"},"total or today_utc; Today uses canonical block-header timestamps"},
  {"utc_start",RPCArg::Type::NUM,RPCArg::Optional::OMITTED,"Returned UTC midnight required with Today continuation; interval [start,start+86400)"}},
 RPCResult{RPCResult::Type::OBJ,"","Native leaderboard snapshot",{
  {RPCResult::Type::STR,"schema","PGN_LEADERBOARD_V1"},
  {RPCResult::Type::STR_HEX,"genesis","Native genesis"},{RPCResult::Type::STR_HEX,"bestblockhash","Exact snapshot tip"},
  {RPCResult::Type::NUM,"height","Snapshot height"},{RPCResult::Type::NUM,"total","Productive operator count"},
  {RPCResult::Type::ARR,"rows","Ranked operators",{{RPCResult::Type::OBJ,"","Operator",{
   {RPCResult::Type::STR_HEX,"operator_id","Native historical operator identity"},
   {RPCResult::Type::STR,"tickets_reported","Exact confirmed report count"},
   {RPCResult::Type::STR,"tickets_validated","Exact natively qualified report count"},
   {RPCResult::Type::STR,"audits_finalized_valid","Exact passed eligible audit-job count"},
   {RPCResult::Type::STR,"validated_search_positions","Exact sum of qualified report range lengths"}}}}}}},RPCExamples{""},
 [](const RPCMethod&,const JSONRPCRequest& request)->UniValue{
  const auto offset=request.params[0].isNull()?0:request.params[0].getInt<int64_t>();
  const auto limit=request.params[1].isNull()?100:request.params[1].getInt<int64_t>();
  if(offset<0||limit<1||limit>100)throw JSONRPCError(RPC_INVALID_PARAMETER,"Leaderboard offset>=0 and limit 1 through 100 required");
  std::optional<uint256> expected;
  if(!request.params[2].isNull()){
   const auto text=request.params[2].get_str();expected=uint256::FromHex(text);
   if(!expected||expected->GetHex()!=text)throw JSONRPCError(RPC_INVALID_PARAMETER,"Canonical snapshot tip required");
  }
  const auto period=request.params[3].isNull()?std::string{"total"}:request.params[3].get_str();
  if(period!="total"&&period!="today_utc")throw JSONRPCError(RPC_INVALID_PARAMETER,"Leaderboard period must be total or today_utc");
  LeaderboardUTCWindow window;bool allow_build=true;
  if(period=="today_utc"){
   const auto today=LeaderboardUTCWindow::TodayAt(GetTime());
   if((expected||offset>0)&&request.params[4].isNull())throw JSONRPCError(RPC_INVALID_PARAMETER,"Today continuation requires fixed anchor and utc_start");
   if(offset>0&&!expected)throw JSONRPCError(RPC_INVALID_PARAMETER,"Today continuation requires fixed anchor and utc_start");
   try{window=request.params[4].isNull()?today:LeaderboardUTCWindow::FromStart(request.params[4].getInt<int64_t>());}
   catch(const std::exception& error){throw JSONRPCError(RPC_INVALID_PARAMETER,error.what());}
   if(!expected&&window!=today)throw JSONRPCError(RPC_INVALID_PARAMETER,"A new Today request must use the current UTC day");
   allow_build=window==today; // prior-day continuation only if already retained
  }else if(!request.params[4].isNull())throw JSONRPCError(RPC_INVALID_PARAMETER,"UTC interval is only available for today_utc");
  return NativeLeaderboardPage(*ReadLeaderboardSnapshot(EnsureAnyChainman(request.context),expected,window,allow_build),uint64_t(offset),unsigned(limit));
 }};
}
