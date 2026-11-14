#include <pgn/hourly_return_snapshot.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace pgn::discussion;
static unsigned checks=0;
static void need(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
static ReceiptId id(uint64_t n){ReceiptId r{};for(unsigned i=0;i<8;++i)r[31-i]=(n>>(8*i))&255;return r;}
int main(){
    const int64_t hour=1794654000; // Aligned UTC hour, independent of local TZ.
    need(hour%3600==0,"fixture hour");
    const HourlyCalibration model{24,10,1};
    std::vector<AdmittedReturn> rows;
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<10;++j)
        rows.push_back({id(rows.size()+1),hour-301-int64_t(i)*600,true,{}});
    auto counts=ProjectHourlyReturns(hour,rows);
    need(counts&&*counts==std::array<uint64_t,6>{10,10,10,10,10,10},"six equal blocks");
    need(WeightedReturns(*counts)==210,"exact weighted count");
    auto s=PrepareHourlySnapshot(hour,id(1),id(2),*counts,Observation::Complete,model,300);
    need(s&&s->target_tenths==282,"60 returns/hour -> nearest tenth28.2");
    auto zero=PrepareHourlySnapshot(hour,id(1),id(2),{},Observation::Complete,model,300);
    need(zero&&zero->target_tenths==275&&zero->reason==HourlyReason::TrueZero,"real zero floor");
    auto missing=PrepareHourlySnapshot(hour,id(1),id(2),{},Observation::Unavailable,model,300);
    need(missing&&missing->target_tenths==300&&missing->reason==HourlyReason::UnavailableRetained,"missing never becomes zero");
    auto startup=PrepareHourlySnapshot(hour,id(1),id(2),*counts,Observation::Startup,model,300);
    need(startup&&startup->target_tenths==275,"startup floor");
    auto fixed=PrepareHourlySnapshot(hour,id(1),id(2),{},Observation::Complete,model,300,s);
    need(fixed&&*fixed==*s,"later invalidity cannot rewrite applied snapshot");
    need(!PrepareHourlySnapshot(hour,id(3),id(2),{},Observation::Complete,model,300,s),"fork anchor cannot inherit old snapshot");
    need(!PrepareHourlySnapshot(hour,id(1),id(3),{},Observation::Complete,model,300,s),"calibration change cannot overwrite hour");
    auto next=PrepareHourlySnapshot(hour+3600,id(3),id(2),{},Observation::Complete,model,282);
    need(next&&next->target_tenths==275&&s->target_tenths==282,"next hour independent, old target intact");
    rows.insert(rows.begin()+1,rows.front());
    need(ProjectHourlyReturns(hour,rows)==counts,"exact duplicate once");
    rows[1].first_received_utc--;
    need(!ProjectHourlyReturns(hour,rows),"conflicting duplicate rejected");
    rows.erase(rows.begin()+1);
    std::swap(rows[0],rows[1]);need(!ProjectHourlyReturns(hour,rows),"unsorted stream refused");std::swap(rows[0],rows[1]);
    std::vector<AdmittedReturn> edges{{id(1),hour-3900,true,{}},{id(2),hour-300,true,{}},
        {id(3),hour-3901,true,{}},{id(4),hour-301,false,{}},
        {id(5),hour-301,true,hour},{id(6),hour-301,true,hour+1}};
    auto e=ProjectHourlyReturns(hour,edges);
    need(e&&*e==std::array<uint64_t,6>{1,0,0,0,0,1},"half-open edges, expired and known-invalid excluded, future failure retained");
    auto huge=std::array<uint64_t,6>{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};
    need(WeightedReturns(huge)==Integer{UINT64_MAX}*21,"no uint64 weighted overflow");
    need(PrepareHourlySnapshot(hour,id(1),id(2),huge,Observation::Complete,model,300)->target_tenths>600,"large network supported by exact math");
    need(!PrepareHourlySnapshot(hour,id(1),id(2),{},Observation::Startup,{24,0,1},300),"invalid calibration not hidden by startup");
    std::vector<AdmittedReturn> scale;scale.reserve(900000);
    for(uint64_t i=1;i<=900000;++i)scale.push_back({id(i),hour-301,true,{}});
    auto start=std::chrono::steady_clock::now();auto folded=ProjectHourlyReturns(hour,scale);
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    need(folded&&(*folded)[0]==900000,"900000 returns folded exactly");
    std::cout<<"{\"PASS\":true,\"checks\":"<<checks<<",\"receipts\":900000,\"fold_ms\":"<<ms
        <<",\"row_bytes\":"<<sizeof(AdmittedReturn)<<",\"additional_projection_storage\":\"constant\",\"live_or_network_changed\":false}"<<std::endl;
}
