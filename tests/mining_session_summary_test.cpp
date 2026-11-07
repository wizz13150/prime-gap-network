#include "ui/mining_session_summary.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPixmap>
#include <QThread>
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
    const auto render=[&](){app.processEvents();QPixmap image(view.size());view.render(&image);return image;};
    const auto verifyReadableLayout=[&](){
        const auto rows=view.property("pgn_summary_layout_regions").toJsonArray();require(!rows.isEmpty());
        double bottom=-1e9;for(const auto& item:rows){const auto row=item.toObject();require(row.value("font_px").toInt()>=11);require(row.value("y").toDouble()>=bottom);require(row.value("x").toDouble()>=0&&row.value("x").toDouble()+row.value("width").toDouble()<=view.width());bottom=row.value("y").toDouble()+row.value("height").toDouble();}
        auto* scroll=view.findChild<QScrollBar*>("pgn-summary-scroll");require(scroll!=nullptr);
        if(scroll->isVisible()){
            scroll->setValue(scroll->maximum());render();
            const auto network=view.property("pgn_network_summary_regions").toJsonArray();require(network.size()==5);
            const auto last=network.last().toObject();require(last.value("y").toDouble()>=32&&last.value("y").toDouble()+last.value("height").toDouble()<=view.height());
            auto* session=view.findChild<QPushButton*>("pgn-summary-session");require(session->geometry().bottom()<32);
            scroll->setValue(0);render();
        }else require(bottom<=view.height());
    };
    for(const auto& size:QList<QSize>{{147,260},{275,560}})for(int bits:{68,95}){
        const auto start=bits==68?QString("150000000000000000000"):QString("30000000000000000000000000000"),end=bits==68?QString("150000200000000000000"):QString("30000000000000200000000000000");
        view.resize(size);view.present(scope,scope,QJsonObject{{"bits",bits},{"minimum_gap",bits==68?954:1350},{"start",start},{"end",end}});view.show();app.processEvents();QPixmap image(view.size());view.render(&image);
        require(view.property("pgn_summary_range_start")==start&&view.property("pgn_summary_range_end")==end);
        const auto regions=view.property("pgn_summary_range_regions").toJsonArray();require(regions.size()==1&&regions.first().toObject().value("text").toString().contains(QString::number(bits)));verifyReadableLayout();
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
        const auto rows=view.property("pgn_productivity_regions").toJsonArray();require(rows.size()==1&&rows.first().toObject().value("label")=="Tickets/h"&&rows.first().toObject().value("value")=="≈ 18");verifyReadableLayout();
        if(argc>1)require(image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QString("productivity-%1x%2.png").arg(size.width()).arg(size.height()))));
    }
    auto* history=view.findChild<QPushButton*>("pgn-summary-history");history->click();require(history->isChecked()&&view.property("pgn_summary_scope")=="history");require(view.property("pgn_summary_range_start")=="30000000000000000000000000000");
    const QJsonObject observedNetwork{{"schema","PGN_NETWORK_SUMMARY_V1"},{"available",true},{"genesis",QString(64,'a')},{"parent",QString(64,'b')},{"registered_operators",3},{"reporting_operators_in_window",2},{"pending_audits",39},{"merit_target_q16",29*65536},{"window_seconds",600},{"reported_positions","600000000000000"},{"rate_scope","reported_search_excludes_audits_not_hardware_capacity"}};
    QJsonObject networkPacket{{"bits",68},{"minimum_gap",954},{"start","150000000000000000000"},{"end","150000200000000000000"},{"network_admission",QJsonObject{{"schema","PGN_NORMAL_MINER_ADMISSION_OBSERVATION_V1"},{"state","ready"},{"available",true},{"age_seconds",0},{"status",QJsonObject{{"schema","PGN_NORMAL_MINER_JOIN_STATUS_V1"},{"genesis",QString(64,'a')},{"parent",QString(64,'b')},{"network_summary",observedNetwork}}}}}};
    auto qualified=pgn::ui::networkMiningSummary(networkPacket);require(qualified.value("available")==true&&qualified.value("rows").toArray().size()==5);require(!pgn::ui::networkMiningSummary(networkPacket,90001).value("available").toBool());
    auto stale=networkPacket;stale.insert("source_stale",true);require(!pgn::ui::networkMiningSummary(stale).value("available").toBool());
    auto forged=networkPacket;auto observation=forged.value("network_admission").toObject();auto status=observation.value("status").toObject();status.insert("parent",QString(64,'c'));observation.insert("status",status);forged.insert("network_admission",observation);require(!pgn::ui::networkMiningSummary(forged).value("available").toBool());
    for(const auto& size:QList<QSize>{{147,260},{275,469},{275,470},{275,560},{420,780}}){
        view.resize(size);view.present(scope,scope,networkPacket);render();verifyReadableLayout();const auto rows=view.property("pgn_network_summary_regions").toJsonArray();require(rows.size()==5);
        const auto rates=view.property("pgn_productivity_regions").toJsonArray();double bottom=rates.last().toObject().value("y").toDouble()+rates.last().toObject().value("height").toDouble();for(const auto& item:rows){const auto row=item.toObject();require(row.value("y").toDouble()>=bottom);bottom=row.value("y").toDouble()+row.value("height").toDouble();}
        if(argc>1){const auto dir=QDir(QString::fromLocal8Bit(argv[1]));require(render().save(dir.filePath(QString("network-summary-%1x%2.png").arg(size.width()).arg(size.height()))));auto* scroll=view.findChild<QScrollBar*>("pgn-summary-scroll");if(scroll->isVisible()){scroll->setValue(scroll->maximum());require(render().save(dir.filePath(QString("network-summary-%1x%2-bottom.png").arg(size.width()).arg(size.height()))));scroll->setValue(0);}}
    }
    // Real widget clock and adapter, synthetic counters: no mining benchmark.
    QElapsedTimer measured;measured.start();QJsonObject measuredDevice{{"id","observed-widget"},{"kind","GPU"},{"label","Test GPU"},{"active",true},{"selected",true},{"counter_fresh",true},{"counter_epoch","widget-fixture"},{"positions_per_second",1e12},{"completed_positions","0"}};
    view.presentProductivity({measuredDevice},"200000000000000");
    while(measured.elapsed()<10'200){QThread::msleep(100);measuredDevice.insert("completed_positions",QString::number(measured.elapsed()*1'000'000'000LL));view.presentProductivity({measuredDevice},"200000000000000");app.processEvents();}
    observation=view.property("pgn_ticket_productivity").toJsonObject().value("observed").toObject();
    require(observation.value("total_tickets_per_hour").isDouble());require(std::abs(observation.value("total_tickets_per_hour").toDouble()-18.)<.05);
    while(measured.elapsed()<15'400){QThread::msleep(100);view.presentProductivity({measuredDevice},"200000000000000");app.processEvents();}
    observation=view.property("pgn_ticket_productivity").toJsonObject().value("observed").toObject();require(observation.value("total_tickets_per_hour").toDouble()>10&&observation.value("total_tickets_per_hour").toDouble()<14);
    render();const auto observedRow=view.property("pgn_productivity_regions").toJsonArray().first().toObject();require(!observedRow.value("value").toString().startsWith("≈"));
    if(argc>1)require(render().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("observed-wait-inclusive.png")));
    measuredDevice.insert("counter_fresh",false);view.presentProductivity({measuredDevice},"200000000000000");require(view.property("pgn_ticket_productivity").toJsonObject().value("observed").toObject().value("total_tickets_per_hour").isNull());
    using Integer=boost::multiprecision::cpp_int;
    const Integer bandLow=Integer{1}<<67,bandHigh=Integer{1}<<68,origin("150000000000000000000"),excluded=origin-bandLow;
    const auto decimal=[](const Integer& n){return QString::fromStdString(n.str());};
    const auto ppb=[&](const Integer& n){return (n*1000000000/bandLow).convert_to<qint64>();};
    const QJsonObject band{{"bits",68},{"lower",decimal(bandLow)},{"upper",decimal(bandHigh)},
        {"search_start",decimal(origin)},{"search_end",decimal(bandHigh)},{"excluded_prefix",decimal(excluded)},
        {"reported_positions","200000000000000"},{"audited_positions","100000000000000"},
        {"excluded_ppb",ppb(excluded)},{"reported_ppb",ppb(Integer("200000000000000"))},{"audited_ppb",ppb(Integer("100000000000000"))}};
    const QJsonObject coverage{{"schema","PGN_BIT_RANGE_COVERAGE_V1"},{"scope","unique_reported_ranges_audited_subset_excluded_prefix_separate"},
        {"origin",decimal(origin)},{"limit",decimal(Integer{1}<<95)},{"bands",QJsonArray{band}}};
    const QJsonObject tip{{"hash",QString(64,'b')},{"height",1},{"time",1000},{"reported_positions","200000000000000"},
        {"audited_positions","100000000000000"},{"window_seconds",600},{"merit_target_q16",29*65536}};
    QJsonObject core{{"schema","PGN_NETWORK_METRICS_V1"},{"genesis",QString(64,'a')},{"bestblockhash",QString(64,'b')},
        {"height",1},{"median_time_past",1000},{"rate_scope","reported_search_positions_per_chain_second_excludes_audits"},
        {"audit_validation_scope","historical_reports_qualified_at_snapshot_tip"},{"points",QJsonArray{tip}},
        {"audit_enabled_operators",3},{"reporting_operators_in_window",2},
        {"audit_queue",QJsonObject{{"awaiting",2},{"awaiting_seed",0},{"assigned",0},{"committed",0}}},{"range_coverage",coverage}};
    require(pgn::ui::networkBitCoverage(core,QString(64,'a'),68).value("available")==true);
    require(!pgn::ui::networkBitCoverage(core,QString(64,'a'),69).value("available").toBool());
    require(!pgn::ui::networkBitCoverage(core,QString(64,'c'),68).value("available").toBool());
    require(!pgn::ui::networkBitCoverage(core,QString(64,'a'),68,90001).value("available").toBool());
    for(const auto& key:QStringList{"lower","upper","search_start","search_end","excluded_prefix","reported_positions","audited_positions","reported_ppb"}){
        auto bad=core,changed=coverage,badBand=band;badBand.insert(key,key=="reported_ppb"?QJsonValue(-1):QJsonValue("99999999999999999999999999999999"));changed.insert("bands",QJsonArray{badBand});bad.insert("range_coverage",changed);
        require(!pgn::ui::networkBitCoverage(bad,QString(64,'a'),68).value("available").toBool());
    }
    auto duplicateBands=coverage;duplicateBands.insert("bands",QJsonArray{band,band});auto badCore=core;badCore.insert("range_coverage",duplicateBands);
    require(!pgn::ui::networkBitCoverage(badCore,QString(64,'a'),68).value("available").toBool());
    require(pgn::ui::coveragePercent(0)=="0%"&&pgn::ui::coveragePercent(0,true)=="<0.0000001%"&&pgn::ui::coveragePercent(1)=="0.0000001%"&&pgn::ui::coveragePercent(1000000000)=="100%");
    for(const auto& size:QList<QSize>{{147,260},{275,469},{275,560},{420,780}}){
        view.resize(size);view.present(scope,scope,networkPacket);view.presentNetworkMetrics(core,QString(64,'a'));render();verifyReadableLayout();
        require(view.property("pgn_bit_range_coverage").toJsonObject().value("available")==true);
        const auto regions=view.property("pgn_bit_range_coverage_regions").toJsonArray();require(regions.size()==3);
        double end=-1e9;for(const auto& item:regions){const auto r=item.toObject();require(r.value("font_px").toInt()>=11&&r.value("y").toDouble()>=end);end=r.value("y").toDouble()+r.value("height").toDouble();}
        auto* scroll=view.findChild<QScrollBar*>("pgn-summary-scroll");scroll->setValue(scroll->maximum());
        if(argc>1)require(render().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QString("bit-coverage-%1x%2-bottom.png").arg(size.width()).arg(size.height()))));
        scroll->setValue(0);
    }
    auto legacyCore=core;legacyCore.remove("range_coverage");view.presentNetworkMetrics(legacyCore,QString(64,'a'));render();
    require(!view.property("pgn_bit_range_coverage").toJsonObject().value("available").toBool()&&view.property("pgn_bit_range_coverage_regions").toJsonArray().isEmpty());
    std::cout<<"PASS "<<checks<<" range precision, compact/large geometry, session/history, actual cadence and exact bit-coverage admission\n";
}
