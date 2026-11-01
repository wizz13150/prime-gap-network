#include "ui/mining_session_summary.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPixmap>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    QApplication app(argc,argv);QTemporaryDir temporary;if(!temporary.isValid())return 2;
    QCoreApplication::setOrganizationName("PGN-TEST");QCoreApplication::setApplicationName("summary-range");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    int checks=0;const auto require=[&](bool okay){++checks;if(!okay)throw std::runtime_error("range summary check "+std::to_string(checks));};
    using pgn::ui::positionRangeFormats;
    auto range=positionRangeFormats("150000000000000000000","150000200000000000000");require(range.valid&&range.start.decimal=="1.5e20"&&range.end.decimal=="1.500002e20");
    for(const auto& pair:QList<QPair<QString,QString>>{{"30000000000000000000000000000","30000000000000200000000000000"},{"100000000000000000001","100000000000000000002"},{"999999999999999999999","1000000000000000000000"}}){range=positionRangeFormats(pair.first,pair.second);require(range.valid&&range.start.decimal!=range.end.decimal&&range.start.exact==pair.first&&range.end.exact==pair.second);}
    require(!positionRangeFormats("10","9").valid&&!positionRangeFormats("1","1").valid&&!positionRangeFormats("bad","2").valid);
    const QJsonObject scope{{"statistics",QJsonObject{{"larger_gaps","731"}}},{"positions","1800000000000000"},{"tests","4650000000000"},{"completed_tickets","9"},{"ticket_count","10"},{"observed_golds",1},{"active_time_ns","1655000000000"},{"best",QJsonArray{QJsonObject{{"gap",1352}}}}};
    pgn::ui::MiningSessionSummary view;view.setStyleSheet("QWidget{background:#010b10;color:#c9eee7;}");
    for(const auto& size:QList<QSize>{{147,260},{275,560}})for(int bits:{68,95}){
        const auto start=bits==68?QString("150000000000000000000"):QString("30000000000000000000000000000"),end=bits==68?QString("150000200000000000000"):QString("30000000000000200000000000000");
        view.resize(size);view.present(scope,scope,QJsonObject{{"bits",bits},{"minimum_gap",bits==68?954:1350},{"start",start},{"end",end}});view.show();app.processEvents();QPixmap image(view.size());view.render(&image);
        require(view.property("pgn_summary_range_start")==start&&view.property("pgn_summary_range_end")==end);
        const auto regions=view.property("pgn_summary_range_regions").toJsonArray();require(regions.size()==4);
        double bottom=0;for(const auto& item:regions){const auto r=item.toObject();require(r.value("y").toDouble()>=bottom&&r.value("y").toDouble()+r.value("height").toDouble()<=view.height()&&r.value("text_width").toInt()<=r.value("width").toInt());bottom=r.value("y").toDouble()+r.value("height").toDouble();}
        if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));require(image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QString("summary-%1x%2-%3bits.png").arg(size.width()).arg(size.height()).arg(bits))));}
    }
    const QJsonArray devices{QJsonObject{{"id","GPU/0"},{"kind","GPU"},{"label","RTX 3090"},{"active",true},{"positions_per_second",1e12}},QJsonObject{{"id","CPU/ALEX/0"},{"kind","CPU"},{"label","CPU ALEX"},{"active",true},{"validation_only",true},{"positions_per_second",1.8e9}},QJsonObject{{"id","CPU/WIZZ/0"},{"kind","CPU"},{"label","CPU WIZZ"},{"active",false}}};
    const auto rate=pgn::ui::ticketProductivity(devices,"200000000000000");
    require(std::abs(rate.value("total_tickets_per_hour").toDouble()-18.0324)<1e-10);
    require(std::abs(rate.value("devices").toArray()[1].toObject().value("tickets_per_hour").toDouble()-.0324)<1e-12);
    auto duplicate=devices;duplicate.append(devices.first());require(pgn::ui::ticketProductivity(duplicate,"200000000000000")==rate);
    for(const auto& width:QStringList{"","0","-2","2e14","0200000000000000"})require(pgn::ui::ticketProductivity(devices,width).value("total_tickets_per_hour").isNull());
    auto missing=devices;auto bad=missing[1].toObject();bad.remove("positions_per_second");missing[1]=bad;
    require(pgn::ui::ticketProductivity(missing,"200000000000000").value("total_tickets_per_hour").isNull());
    bad.insert("positions_per_second",-1);missing[1]=bad;require(pgn::ui::ticketProductivity(missing,"200000000000000").value("total_tickets_per_hour").isNull());
    require(pgn::ui::ticketProductivity({devices.last()},"200000000000000").value("total_tickets_per_hour").isNull());
    for(const auto& size:QList<QSize>{{147,260},{275,560}}){
        view.resize(size);view.presentProductivity(devices,"200000000000000");app.processEvents();QPixmap image(size);view.render(&image);
        const auto rows=view.property("pgn_productivity_regions").toJsonArray();require(rows.size()==(size.height()<350?3:4));
        const auto ranges=view.property("pgn_summary_range_regions").toJsonArray();double bottom=ranges.last().toObject().value("y").toDouble()+ranges.last().toObject().value("height").toDouble();
        for(const auto& item:rows){const auto row=item.toObject();require(row.value("y").toDouble()>=bottom&&row.value("y").toDouble()+row.value("height").toDouble()<=view.height());bottom=row.value("y").toDouble()+row.value("height").toDouble();}
        if(argc>1)require(image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QString("productivity-%1x%2.png").arg(size.width()).arg(size.height()))));
    }
    auto* history=view.findChild<QPushButton*>("pgn-summary-history");history->click();require(history->isChecked()&&view.property("pgn_summary_scope")=="history");require(view.property("pgn_summary_range_start")=="30000000000000000000000000000");
    std::cout<<"PASS "<<checks<<" range precision, exact boundary retention, compact/large geometry and independent session/history checks\n";
}
