#include "ui/observed_ticket_productivity.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using pgn::ui::ObservedTicketProductivity;
static QJsonObject device(QString id,QString count,QString epoch="one",bool fresh=true,bool selected=true,QString kind="GPU"){
 return {{"id",id},{"kind",kind},{"label",id},{"selected",selected},{"counter_epoch",epoch},{"completed_positions",count},{"counter_fresh",fresh}};
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);int checks=0;auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("observed productivity check "+std::to_string(checks));};
 const QString width="200000000000000";constexpr qint64 sec=1'000'000'000LL;
 auto rate=[](const QJsonObject& o){return o.value("total_tickets_per_hour");};
 ObservedTicketProductivity p;require(rate(p.observe({device("gpu","0")},width,0)).isNull());
 p.observe({device("gpu","5000000000000")},width,5*sec);
 auto v=p.observe({device("gpu","10000000000000")},width,10*sec);require(std::abs(rate(v).toDouble()-18.)<1e-10);
 p.observe({device("gpu","10000000000000")},width,15*sec);
 v=p.observe({device("gpu","10000000000000")},width,20*sec);require(std::abs(rate(v).toDouble()-9.)<1e-10); // same work, waiting halves the wall cadence
 require(v.value("devices").toArray()[0].toObject().value("positions_delta")=="10000000000000");
 require(rate(p.observe({device("gpu","0","new")},width,21*sec)).isNull());
 require(rate(p.observe({device("gpu","100","new",false)},width,22*sec)).isNull());
 require(rate(p.observe({device("gpu","100","new")},width,23*sec)).isNull());
 p.observe({device("gpu","100","new")},width,28*sec);v=p.observe({device("gpu","100","new")},width,33*sec);require(rate(v).isDouble()&&rate(v).toDouble()==0.);
 require(rate(p.observe({device("gpu","99","new")},width,34*sec)).isNull()); // rollback does not become negative throughput
 require(rate(p.observe({device("gpu","100","new")},"100000000000000",35*sec)).isNull());
 require(rate(p.observe({device("gpu","200","new")},width,100*sec)).isNull()); // unobserved sleep excluded
 require(rate(p.observe({device("gpu","300","new")},width,99*sec)).isNull()); // backwards clock
 ObservedTicketProductivity exact;const QString large="30000000000000000000000000000";exact.observe({device("g",large)},width,0);exact.observe({device("g",pgn::booking::addUnsigned(large,"50"))},width,5*sec);v=exact.observe({device("g",pgn::booking::addUnsigned(large,"100"))},width,10*sec);require(v.value("devices").toArray()[0].toObject().value("positions_delta")=="100");
 ObservedTicketProductivity both;for(int i=0;i<=20;++i){auto a=device("g",QString::number(qint64(i)*1000000000000LL));auto b=device("c",QString::number(qint64(i)*1000000000LL),"one",true,true,"CPU");v=both.observe({a,b,a,device("off","",{},false,false,"CPU")},width,i*sec);}
 require(std::abs(rate(v).toDouble()-18.018)<1e-10);require(v.value("devices").toArray().size()==3);require(v.value("groups").toArray()[1].toObject().value("tickets_per_hour").toDouble()>0);
 require(rate(both.observe({device("g","21000000000000"),device("c","bad","one",true,true,"CPU")},width,21*sec)).isNull());
 require(rate(both.observe({device("g","21000000000000")},"2e14",22*sec)).isNull());
 require(rate(both.observe({},width,23*sec)).isNull());
 ObservedTicketProductivity bounded;for(int i=0;i<1000;++i)v=bounded.observe({device("g",QString::number(qint64(i)*1000000000000LL))},width,i*sec);
 const auto duration=v.value("devices").toArray()[0].toObject().value("wall_time_ns").toString().toLongLong();require(duration==60*sec&&std::abs(rate(v).toDouble()-18.)<1e-10);
 ObservedTicketProductivity handoff;auto job=device("g","0","rangeA");job.insert("counter_total","100");job.insert("counter_zero_origin",true);handoff.observe({job},"100",0);job.insert("completed_positions","50");handoff.observe({job},"100",5*sec);job.insert("completed_positions","100");handoff.observe({job},"100",10*sec);
 job.insert("counter_epoch","rangeB");job.insert("completed_positions","10");v=handoff.observe({job},"100",15*sec);require(std::abs(rate(v).toDouble()-264.)<1e-10);require(v.value("devices").toArray()[0].toObject().value("positions_delta")=="110");
 job.insert("counter_epoch","rangeC");job.insert("completed_positions","20");require(rate(handoff.observe({job},"100",16*sec)).isNull()); // B tail was not witnessed
 ObservedTicketProductivity dense;for(int i=0;i<10000;++i)v=dense.observe({device("g",QString::number(qint64(i)*10'000'000'000LL))},width,qint64(i)*10'000'000LL);
 require(std::abs(rate(v).toDouble()-18.)<1e-10&&v.value("devices").toArray()[0].toObject().value("wall_time_ns").toString().toLongLong()>=59*sec);
 std::cout<<"PASS "<<checks<<" exact deltas, wait-inclusive cadence, witnessed handoff, CPU/GPU sums, stale/reset/sleep/width boundaries and bounded cadence-independent60s window\n";
}
