#include <pgn/hourly_consensus_quote.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace pgn::discussion;
static unsigned checks=0;
static void need(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
static ReceiptId id(uint64_t n){ReceiptId r{};for(unsigned i=0;i<8;++i)r[31-i]=uint8_t(n>>(8*i));return r;}
int main(){
 const int64_t hour=1794654000;const HourlyCalibration model{24,10,1};
 std::vector<AdmittedReturn> rows;
 for(unsigned i=0;i<6;++i)for(unsigned j=0;j<10;++j)rows.push_back({id(rows.size()+1),hour-301-int64_t(i)*600,true,{}});
 const auto run=[&](CanonicalHistory input,std::span<const AdmittedReturn> data){return PrepareCanonicalHourlyQuote(hour,id(1),id(2),data,input,model,UINT32_MAX);};
 const auto full=run(CanonicalHistory::Complete,rows);
 need(full.verdict==HourlyConsensusVerdict::Ready&&full.quote.has_value(),"complete authenticated input must be usable");
 need(full.quote->target.target_tenths==282,"measured model must stay28.2");
 need(full.quote->target.proof_numerator==282&&full.quote->target.proof_denominator==10,"decimal proof threshold must stay exact");
 need(full.quote->target.header_q16==1848116,"Q16 identifier must be ceiling28.2");
 for(unsigned i=0;i<100;++i){
  const auto missing=run(CanonicalHistory::Unavailable,i%2?std::span<const AdmittedReturn>{rows}:std::span<const AdmittedReturn>{});
  need(missing.verdict==HourlyConsensusVerdict::AwaitHistory&&!missing.quote,"a missing local archive must never authorize an old target or zero");
 }
 const auto zero=run(CanonicalHistory::Complete,{});
 need(zero.verdict==HourlyConsensusVerdict::Ready&&zero.quote->target.target_tenths==275,"authenticated real zero uses floor");
 need(zero.quote->snapshot.reason==HourlyReason::TrueZero,"real zero remains distinct from startup");
 const auto start=run(CanonicalHistory::Startup,{});
 need(start.verdict==HourlyConsensusVerdict::Ready&&start.quote->snapshot.reason==HourlyReason::Startup,"canonical startup uses floor");
 need(run(CanonicalHistory::Startup,rows).verdict==HourlyConsensusVerdict::InvalidInput,"startup must not hide existing history");
 need(run(static_cast<CanonicalHistory>(99),rows).verdict==HourlyConsensusVerdict::InvalidInput,"unknown observation cannot authorize a quote");
 auto conflicting=rows;conflicting.insert(conflicting.begin()+1,conflicting.front());conflicting[1].first_received_utc--;
 const auto bad=run(CanonicalHistory::Complete,conflicting);
 need(bad.verdict==HourlyConsensusVerdict::InvalidInput&&!bad.quote,"conflicting receipts must not become an empty network");
 auto duplicate=rows;duplicate.insert(duplicate.begin()+1,duplicate.front());
 need(run(CanonicalHistory::Complete,duplicate).quote->target.target_tenths==282,"exact replay must not increase target");
 need(PrepareCanonicalHourlyQuote(hour,id(3),id(2),rows,CanonicalHistory::Complete,model,UINT32_MAX).quote->snapshot.activation_anchor==id(3),"reorg anchor remains explicit");
 need(PrepareCanonicalHourlyQuote(hour,{},id(2),rows,CanonicalHistory::Complete,model,UINT32_MAX).verdict==HourlyConsensusVerdict::InvalidInput,"missing canonical anchor rejected");
 need(PrepareCanonicalHourlyQuote(hour,id(1),{},rows,CanonicalHistory::Complete,model,UINT32_MAX).verdict==HourlyConsensusVerdict::InvalidInput,"missing calibration rejected");
 need(PrepareCanonicalHourlyQuote(hour,id(1),id(2),rows,CanonicalHistory::Complete,model,1802240).verdict==HourlyConsensusVerdict::InvalidInput,"unrepresentable quote must not be clamped");
 need(PrepareCanonicalHourlyQuote(hour+1,id(1),id(2),rows,CanonicalHistory::Complete,model,UINT32_MAX).verdict==HourlyConsensusVerdict::InvalidInput,"non-hour input rejected");
 const auto restored=run(CanonicalHistory::Complete,rows);
 need(restored.quote->snapshot==full.quote->snapshot,"restored local history must recover identical canonical snapshot");
 std::cout<<"{\"PASS\":true,\"checks\":"<<checks<<",\"local_unavailability_never_authorizes_target\":true,\"live_changed\":false}\n";
}
