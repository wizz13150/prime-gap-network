#include "ui/cpu_card_metrics.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace pgn::ui;
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);int checks=0;auto require=[&](bool v){if(!v)throw std::runtime_error("CPU card check "+std::to_string(checks));++checks;};
 QJsonObject telemetry{{"schema","PGN_CPU_TELEMETRY_V1"},{"worker_busy_percent",95.},{"host_memory_total_bytes","16000000000"},{"host_memory_used_bytes","8000000000"},{"engine_working_set_bytes","104857600"}};
 QJsonObject state{{"state","RUNNING"},{"enabled",true},{"resident_workers",4},{"requested_threads",4},{"rate_positions_per_second",1.2e9},{"machine_load_percent",44.},{"durable_completed_positions","8000000000000"},{"total_positions","10000000000000"},{"cpu_telemetry",telemetry}};
 state.insert("request_sha256",QString(64,'a'));state.insert("mining_rates",QJsonObject{{"schema","PGN_CPU_MINING_RATES_V1"},{"source","durable_completed_counts"},{"request_sha256",QString(64,'a')},{"positions","8000000000000"},{"tests_per_second",4500000.},{"gaps_per_second",6.},{"gaps_per_billion_positions",5.}});
 auto full=cpuCardMetrics(state,true,true,QColor("#55ffaa"));auto small=cpuCardMetrics(state,true,false,QColor("#55ffaa"));
 require(full.size()==8&&small.size()==4);require(full[0].text=="95%"&&full[2].text=="50%"&&full[3].text=="100");require(full[4].value==1.2e9&&full[5].value==4500000.&&full[6].value==6.&&full[7].value==5.);require(small[1].value==full[4].value&&small[2].value==full[5].value&&small[3].value==full[7].value);
 require(full[4].label=="pos/s"&&full[5].label=="tests/s"&&full[6].label=="gaps/s"&&full[7].label=="gaps/Gpos");for(const auto& m:full)require(m.label!="WORKERS"&&m.label!="WORK %");
 auto paused=state;paused.insert("state","BUDGET_PAUSED");paused.insert("resident_workers",0);paused.insert("rate_positions_per_second",QJsonValue{});auto p=cpuCardMetrics(paused,true,true,QColor());
 require(p[4].value==0.&&p[5].value==0.&&p[6].value==0.&&!p[7].value);require(cpuActivityCaption(paused,true).contains("CPU load protection"));
 auto wait=paused;wait.insert("state","WAITING_AUDIT");wait.insert("total_positions","0");wait.insert("durable_completed_positions","0");auto w=cpuCardMetrics(wait,true,true,QColor());
 require(!w[7].value&&w[4].text=="0"&&w[3].value.has_value());require(cpuActivityCaption(wait,true).contains("waiting"));
 auto starting=state;starting.insert("rate_positions_per_second",QJsonValue{});require(cpuCardMetrics(starting,true,true,QColor())[4].text=="START");
 auto disconnected=cpuCardMetrics(state,false,true,QColor());require(!disconnected[0].value&&!disconnected[4].value&&!disconnected[5].value&&!disconnected[6].value&&!disconnected[7].value);require(cpuActivityCaption(state,false).contains("unconfirmed"));
 auto bad=state;bad.insert("durable_completed_positions","-1");bad.insert("cpu_telemetry",QJsonObject{});auto b=cpuCardMetrics(bad,true,true,QColor());require(!b[3].value&&!b[7].value);
 require(cpuMetricSnapshot(full).size()==8);
 auto idlePages=wait;idlePages.insert("memory_pages",QJsonObject{{"schema","PGN_CPU_MEMORY_PAGES_V1"},{"cause","no_task"},{"large_page_workers",0},{"large_page_bytes_in_use","0"}});require(cpuMemoryPagesPresentation(idlePages,true).tooltip.contains("No CPU worker"));
 require(cpuActivityCaption(wait,true,"Audit registration required")=="AUDIT · Audit registration required");
 auto off=wait;off.insert("enabled",false);require(cpuActivityCaption(off,true,"Audit registration required")=="OFF · no calculation");
 require(!cpuMemoryPagesPresentation(state,true).active);
 auto probeOnly=state;probeOnly.insert("large_pages",QJsonObject{{"allocation_verified",true}});require(!cpuMemoryPagesPresentation(probeOnly,true).active);
 auto allocated=state;allocated.insert("memory_pages",QJsonObject{{"schema","PGN_CPU_MEMORY_PAGES_V1"},{"large_page_workers",4},{"large_page_bytes_in_use","16777216"}});
 require(cpuMemoryPagesPresentation(allocated,true).active&&cpuMemoryPagesPresentation(allocated,true).color==QColor("#69efc5"));
 require(!cpuMemoryPagesPresentation(allocated,false).active);
 auto stoppedAllocation=allocated;stoppedAllocation.insert("resident_workers",0);require(!cpuMemoryPagesPresentation(stoppedAllocation,true).active);
 auto partialPages=allocated;auto pageState=allocated.value("memory_pages").toObject();pageState.insert("large_page_workers",3);partialPages.insert("memory_pages",pageState);require(!cpuMemoryPagesPresentation(partialPages,true).active);
 pageState.insert("large_page_workers",4);pageState.insert("large_page_bytes_in_use","0");partialPages.insert("memory_pages",pageState);require(!cpuMemoryPagesPresentation(partialPages,true).active);
 auto foreign=state;auto wrongRates=foreign.value("mining_rates").toObject();wrongRates.insert("request_sha256",QString(64,'b'));foreign.insert("mining_rates",wrongRates);require(!cpuCardMetrics(foreign,true,true,QColor())[5].value);
 auto legacy=state;legacy.remove("mining_rates");require(!cpuCardMetrics(legacy,true,true,QColor())[5].value);
 std::cout<<"PASS "<<checks<<" CPU presentation checks: real telemetry, shared science units, real durable counters, zero known idle, stale/foreign refusal, startup/disconnect/hugepages\n";
}
