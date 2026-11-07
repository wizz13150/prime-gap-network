#include "wallet/network_metrics_snapshot.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
QJsonObject metricsFixture(){
    QJsonArray points;
    for(int i=0;i<3;++i)points.append(QJsonObject{{"time",400+i*300},{"height",i+1},
        {"hash",QString(64,QChar(i==2?'b':char('c'+i)))},{"merit_target_q16",28*65536+i*17},
        {"gold_count",0},{"window_seconds",300+i*150},{"reported_positions","600000000000000"},{"audited_positions","200000000000000"}});
    return {{"schema","PGN_NETWORK_METRICS_V1"},{"genesis",QString(64,'a')},{"bestblockhash",QString(64,'b')},
        {"height",3},{"median_time_past",1000},{"window_seconds",600},{"span_seconds",86400},
        {"rate_scope","reported_search_positions_per_chain_second_excludes_audits"},
        {"audit_validation_scope","historical_reports_qualified_at_snapshot_tip"},{"points",points}};
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);const auto genesis=QString(64,'a');const auto valid=metricsFixture();
    auto require=[](bool okay){if(!okay)throw std::runtime_error("Network metrics validation failed");};
    require(validateNetworkMetrics(valid,genesis).isEmpty());
    auto bad=valid;bad.insert("genesis",QString(64,'e'));require(!validateNetworkMetrics(bad,genesis).isEmpty());
    bad=valid;bad.insert("rate_scope","sha256_hashes_per_second");require(!validateNetworkMetrics(bad,genesis).isEmpty());
    for(const auto& wrong:QJsonArray{"-1","01","1e20","NaN",1.5}){
        bad=valid;auto points=bad.value("points").toArray();auto point=points[0].toObject();point.insert("reported_positions",wrong);points[0]=point;bad.insert("points",points);
        require(!validateNetworkMetrics(bad,genesis).isEmpty());
    }
    bad=valid;auto points=bad.value("points").toArray();auto point=points[0].toObject();point.insert("audited_positions","600000000000001");points[0]=point;bad.insert("points",points);require(!validateNetworkMetrics(bad,genesis).isEmpty());
    bad=valid;points=bad.value("points").toArray();points[1]=points[0];bad.insert("points",points);require(!validateNetworkMetrics(bad,genesis).isEmpty());
    bad=valid;points=bad.value("points").toArray();point=points.last().toObject();point.insert("hash",QString(64,'f'));points[2]=point;bad.insert("points",points);require(!validateNetworkMetrics(bad,genesis).isEmpty());
    bad=valid;points={};for(int i=0;i<257;++i)points.append(valid.value("points").toArray()[0]);bad.insert("points",points);require(!validateNetworkMetrics(bad,genesis).isEmpty());
    require(metricLessEqual("99999999999999999999999999999999","100000000000000000000000000000000"));
    require(!metricLessEqual("100000000000000000000000000000001","100000000000000000000000000000000"));
    std::cout<<"PASS network metrics: pinned chain, explicit units, exact decimal volume bounds, ordered samples, tip binding and bounded payload\n";
}
