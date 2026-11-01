#include "ui/cpu_card_metrics.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace pgn::ui;
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);int checks=0;auto require=[&](bool v){if(!v)throw std::runtime_error("CPU card check "+std::to_string(checks));++checks;};
 QJsonObject telemetry{{"schema","PGN_CPU_TELEMETRY_V1"},{"worker_busy_percent",95.},{"host_memory_total_bytes","16000000000"},{"host_memory_used_bytes","8000000000"},{"engine_working_set_bytes","104857600"}};
 QJsonObject state{{"state","RUNNING"},{"enabled",true},{"resident_workers",4},{"requested_threads",4},{"rate_positions_per_second",1.2e9},{"machine_load_percent",44.},{"durable_completed_positions","8000000000000"},{"total_positions","10000000000000"},{"cpu_telemetry",telemetry}};
 auto full=cpuCardMetrics(state,true,true,QColor("#55ffaa"));auto small=cpuCardMetrics(state,true,false,QColor("#55ffaa"));
 require(full.size()==8&&small.size()==4);require(full[0].text=="95%"&&full[2].text=="50%"&&full[3].text=="100");require(full[4].text=="4/4"&&full[5].value==1.2e9&&full[7].text=="80.0%");
 for(const auto& m:full)require(m.label!="tests/s"&&m.label!="gaps/Gpos");
 auto paused=state;paused.insert("state","BUDGET_PAUSED");paused.insert("resident_workers",0);paused.insert("rate_positions_per_second",QJsonValue{});auto p=cpuCardMetrics(paused,true,true,QColor());
 require(p[4].text=="0/4"&&p[5].value==0.&&p[7].text=="80.0%");require(cpuActivityCaption(paused,true).contains("CPU load protection"));
 auto wait=paused;wait.insert("state","WAITING_AUDIT");wait.insert("total_positions","0");wait.insert("durable_completed_positions","0");auto w=cpuCardMetrics(wait,true,true,QColor());
 require(w[7].text=="NO JOB"&&w[5].text=="0"&&w[3].value.has_value());require(cpuActivityCaption(wait,true).contains("waiting"));
 auto starting=state;starting.insert("rate_positions_per_second",QJsonValue{});require(cpuCardMetrics(starting,true,true,QColor())[5].text=="START");
 auto disconnected=cpuCardMetrics(state,false,true,QColor());require(!disconnected[0].value&&!disconnected[5].value&&disconnected[6].label=="SAVED"&&disconnected[7].text=="80.0%");require(cpuActivityCaption(state,false).contains("unconfirmed"));
 auto bad=state;bad.insert("durable_completed_positions","-1");bad.insert("cpu_telemetry",QJsonObject{});auto b=cpuCardMetrics(bad,true,true,QColor());require(!b[3].value&&!b[7].value);
 require(cpuMetricSnapshot(full).size()==8);
 std::cout<<"PASS "<<checks<<" CPU presentation checks: real telemetry, no invented PRP/gap counters, zero known idle, preserved progress, startup and disconnect\n";
}
