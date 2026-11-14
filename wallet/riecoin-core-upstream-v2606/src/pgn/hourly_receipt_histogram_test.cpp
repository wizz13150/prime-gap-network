#include <pgn/hourly_receipt_histogram.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using pgn::discussion::HourlyReceiptHistogram;
static unsigned checks{};
static void need(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static std::array<uint64_t,6> reference(const std::vector<int64_t>& raw,int64_t hour){
    std::array<uint64_t,6> out{};
    for(auto time:raw)if(time>=hour-3900&&time<hour-300)++out[size_t((hour-301-time)/600)];
    return out;
}
int main(){try{
    HourlyReceiptHistogram h;const int64_t hour=1794654000;
    std::vector<int64_t> raw{hour-3901,hour-3900,hour-3301,hour-3300,hour-901,hour-900,hour-301,hour-300,hour-1,hour};
    for(auto time:raw)need(h.Add(time),"Valid first receipt rejected");
    need(h.Counts(hour)&&*h.Counts(hour)==reference(raw,hour),"UTC boundary projection differs");
    need(!h.Counts(hour+1)&&!h.Counts(-3600),"Non-hour or negative clock accepted");
    const auto stable=*h.Counts(hour);
    need(!h.Add(-1)&&!h.Add(HourlyReceiptHistogram::maximum_clock+1),"Out-of-domain source clock accepted");
    need(*h.Counts(hour)==stable,"Rejected source clock changed the old quote");
    HourlyReceiptHistogram property;std::vector<int64_t> events;
    uint64_t random=7;int64_t time=1609459200;
    for(unsigned i=0;i<20000;++i){random=random*6364136223846793005ULL+1;time+=int64_t((random>>32)%1200);events.push_back(time);need(property.Add(time),"Ordered receipt sequence rejected");}
    for(unsigned i=0;i<200;++i){const auto at=(events.front()+int64_t(i)*(events.back()-events.front())/199)/3600*3600;need(property.Counts(at)&&*property.Counts(at)==reference(events,at),"Bucket projection differs from independent raw-event scan");}
    HourlyReceiptHistogram capacity;
    for(unsigned i=0;i<6;++i)for(unsigned miner=0;miner<50000;++miner)need(capacity.Add(hour-3900+int64_t(i)*600),"Concurrent receipt aggregate lost");
    const auto fifty=capacity.Counts(hour);need(fifty&&std::all_of(fifty->begin(),fifty->end(),[](auto n){return n==50000;}),"50000 receipts per window miscounted");
    HourlyReceiptHistogram years;const int64_t first=946684800,last=first+int64_t(30)*365*86400;
    need(years.Add(first)&&years.Add(last),"Thirty-year receipt span rejected");
    need(years.bins.size()*sizeof(uint64_t)<=30ULL*365*24*6*8+8,"Historical counters expanded beyond one uint64 per10minutes");
    need(years.Counts(first/3600*3600+3600)&&*years.Counts(first/3600*3600+3600)==reference({first,last},first/3600*3600+3600),"Future events changed historical counts");
    HourlyReceiptHistogram end;need(end.Add(HourlyReceiptHistogram::maximum_clock)&&end.bins.size()==1,"Last wire timestamp expanded an empty prefix");
    HourlyReceiptHistogram malformed;malformed.first_bin=1;need(!malformed.Counts(hour),"Malformed empty histogram accepted");
    malformed.bins={0};need(!malformed.Counts(hour),"Malformed counter edge accepted");
    HourlyReceiptHistogram overflow;need(overflow.Add(hour),"Overflow fixture rejected");overflow.bins[0]=UINT64_MAX;
    need(!overflow.Add(hour)&&overflow.bins[0]==UINT64_MAX,"Receipt count silently wrapped");
    std::cout<<"{\"PASS\":true,\"checks\":"<<checks<<",\"raw_event_equivalence\":true,\"utc_boundaries\":true,\"fifty_thousand_per_window\":true,\"thirty_year_counters_bytes\":"<<years.bins.size()*sizeof(uint64_t)<<",\"not_network_load_test\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
