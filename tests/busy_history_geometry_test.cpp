#include "ui/busy_history_geometry.h"
#include <QDateTime>
#include <iostream>
#include <stdexcept>
struct Sample {QDateTime observedUtc;bool utilizationEmitted;double utilization;};
int main(){
    int checks=0;
    auto need=[&](bool ok){if(!ok)throw std::runtime_error("Busy-history geometry check "+std::to_string(checks));++checks;};
    const qint64 now=1700000120000LL;
    const auto sample=[&](qint64 age,double value,bool emitted=true){return Sample{QDateTime::fromMSecsSinceEpoch(now-age,Qt::UTC),emitted,value};};
    const QRectF area(10,20,120,100);
    // Irregular intervals remain 5px and 10px, rather than equal-width steps.
    auto p=pgn::ui::busyHistoryGeometry(QVector<Sample>{sample(20000,100),sample(15000,0),sample(5000,50)},area,now);
    need(p.size()==3);need(p[0].position==QPointF(110,20));
    need(p[1].position==QPointF(115,120)&&p[1].connect);
    need(p[2].position==QPointF(125,70)&&p[2].connect);
    // An absent observation is not a zero; long gaps have no connecting line.
    p=pgn::ui::busyHistoryGeometry(QVector<Sample>{sample(40000,100),sample(35000,0,false),sample(30000,50),sample(5000,100)},area,now);
    need(p.size()==3&&!p[1].connect&&!p[2].connect);
    p=pgn::ui::busyHistoryGeometry(QVector<Sample>{sample(125000,100),sample(5000,50),sample(-1000,10)},area,now);
    need(p.size()==1&&p[0].position.x()==125);
    p=pgn::ui::busyHistoryGeometry(QVector<Sample>{sample(5000,50),sample(10000,20),sample(5000,50)},area,now);
    need(p.size()==3&&!p[1].connect&&p[2].connect);
    need(pgn::ui::busyHistoryGeometry(QVector<Sample>{sample(0,0)},area,now)[0].position.y()==120);
    need(pgn::ui::busyHistoryGeometry(QVector<Sample>{sample(0,std::nan(""))},area,now).isEmpty());
    std::cout<<"PASS "<<checks<<" checks: elapsed time, genuine zero, missing/stale/future data, order and isolated sample\n";
}
