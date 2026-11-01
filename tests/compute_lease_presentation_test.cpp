#include "ui/compute_lease_presentation.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);using namespace pgn::ui;int checks=0;
    const auto require=[&](bool yes){++checks;if(!yes)throw std::runtime_error("lease display check "+std::to_string(checks));};
    require(computeLeaseDeadlineText({{"deadline",432}})=="Ledger record 432");
    const QJsonObject timed{{"deadline_clock","median_chain_time"},{"deadline_time",1793520000}};
    require(computeLeaseDeadlineText(timed,QTimeZone(3600))=="2026-11-01 09:00");
    require(computeLeaseDeadlineText(timed,QTimeZone(-18000))=="2026-11-01 03:00");
    require(computeLeaseDeadlineHeading(timed).contains("local"));
    auto historical=timed;historical.insert("report_root","retained");require(computeLeaseDeadlineHeading(historical)=="Original compute lease (historical)");
    require(computeLeaseDeadlineText({{"deadline_clock","median_chain_time"},{"deadline",432}})=="Unavailable");
    require(computeLeaseDeadlineText({{"deadline_clock","unknown"},{"deadline",432}})=="Unavailable");
    require(computeLeaseDeadlineHeading({{"deadline_clock","unknown"}})=="Compute lease deadline");
    require(computeLeaseDeadlineText({{"deadline_clock",QJsonValue{}},{"deadline",432}})=="Unavailable");
    for(const auto& invalid:QJsonArray{QJsonValue{},QJsonValue(true),QJsonValue("432"),QJsonValue(-1),QJsonValue(0),QJsonValue(1.5)})require(computeLeaseDeadlineText({{"deadline",invalid}})=="Unavailable");
    std::cout<<"PASS "<<checks<<" legacy/time deadline display checks: explicit clocks, local offsets, historical retention, absent/unknown/invalid fields never become zero or another clock\n";
}
