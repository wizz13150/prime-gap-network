#include <pgn/issuance_history.h>
#include <pgn/gold_epoch_policy.h>
#include <streams.h>
#include <cassert>
#include <iostream>
using namespace pgn::core;
uint256 Id(unsigned n){uint256 id;id.begin()[0]=n;return id;}
int main(){
 const CAmount coin=PGN_ATOMIC_UNITS_PER_COIN;
 IssuanceSnapshot before{Id(1),PGN_MAX_SUPPLY_ATOMIC,0,0};before.partition=IssuanceBudgetPartition{Id(3),99000000*coin,0,0,0,0};before.pools=OrdinaryRewardPools{};assert(before.Valid());
 IssuanceSnapshot after=before;after.tip=Id(2);after.minted=171*coin;after.promised=153*coin/2;after.partition->ordinary_minted=after.minted;after.partition->ordinary_promised=after.promised;
 after.pools=OrdinaryRewardPools{64*coin,25*coin/2};after.epochs=GoldEpochBalances{1,3,0,32*coin,24*coin,8*coin};assert(after.Valid());
 DataStream encoded;encoded<<after;assert(uint8_t(encoded.data()[0])==4);IssuanceSnapshot decoded;encoded>>decoded;assert(encoded.empty()&&decoded==after);
 DataStream legacy;legacy<<before;assert(uint8_t(legacy.data()[0])==3);IssuanceSnapshot old;legacy>>old;assert(old==before&&!old.epochs);
 const IssuanceCommit forward{before,after};assert(forward.Valid());const auto rewind=InvertIssuanceCommit(forward,after);assert(rewind&&rewind->after==before&&rewind->before==after);
 auto invalid=after;invalid.epochs->silver++;assert(!invalid.Valid());bool refused=false;try{DataStream stream;stream<<invalid;}catch(const std::ios_base::failure&){refused=true;}assert(refused);
 invalid=after;invalid.epochs->audit_paid_height=4;assert(!invalid.Valid());
 const IssuanceHistoryStep step{IssuanceDirection::Forward,forward};DataStream record;record<<step;IssuanceHistoryStep loaded;record>>loaded;assert(record.empty()&&loaded==step);
 std::cout<<"{\"PASS\":true,\"checks\":12,\"scope\":\"Version3 preserved; version4 epoch/pots roundtrip and exact undo; invalid pool and height rejected\"}\n";
}
