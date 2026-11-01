#include "ui/engine_tools_view.h"
#include "telemetry.h"
#include "horizon_workspace_style.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QTableWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QFileDialog>
#include <QTimer>
#include <iostream>
#include <memory>
int main(int argc,char** argv){
 QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
 QApplication app(argc,argv);app.setStyleSheet(HorizonWorkspaceControlStyle());QTemporaryDir root;
 std::unique_ptr<QWidget> bench(pgn::ui::createBenchmarkView(root.path())),config(pgn::ui::createEngineConfigurationView(root.path()));
 bench->resize(1280,720);bench->show();app.processEvents();
 auto* measure=bench->findChild<QPushButton*>("pgn-benchmark-measure");auto* cancel=bench->findChild<QPushButton*>("pgn-benchmark-cancel");
 FrontierTelemetrySnapshot source;source.authoritative=true;source.runId="actual-run";source.gpuUuid="actual-gpu";source.gpuName="RTX3060";source.domainStart="199999990000000000000";source.domainEnd="200000000000000000000";source.engineProfile="GPU WALK";source.engineId="pgn-native-walk";
 source.observedUtc=QDateTime::currentDateTimeUtc();source.rowsCompleted="100";source.cumulativeGapCandidates="10";
 source.raw={{"pgn_process_running",true},{"pgn_clock_namespace","test-source-clock"},{"pgn_source_time_ns","1000000000"},{"pgn_cumulative_tests","25"}};
 pgn::ui::presentEngineTools(bench.get(),source);pgn::ui::presentEngineTools(config.get(),source);
 auto* sourceTable=bench->findChild<QTableWidget*>("pgn-engine-source");auto* sourceCell=sourceTable->item(0,0);sourceTable->setCurrentCell(0,0);sourceTable->selectRow(0);pgn::ui::presentEngineTools(bench.get(),source);
 if(sourceCell!=sourceTable->item(0,0)||sourceTable->currentRow()!=0)return 15;
 const auto profileValue=[&](const QString& key){for(int i=0;i<sourceTable->rowCount();++i)if(sourceTable->item(i,0)->text()==key)return sourceTable->item(i,1)->text();return QString{};};
 if(profileValue("version")!="Unavailable"||profileValue("engine_artifact_sha256")!="Unavailable"||profileValue("number_bits")!="Unavailable")return 17;
 source.numberBits=68;pgn::ui::presentEngineTools(bench.get(),source);
 if(profileValue("number_bits")!="68"||profileValue("version")!="Unavailable")return 18;
 if(!measure->isEnabled())return 2;measure->click();if(measure->isEnabled()||!cancel->isEnabled())return 3;
 source.raw["pgn_source_time_ns"]="30000000000";source.rowsCompleted="5900";source.raw["pgn_cumulative_tests"]="5825";source.cumulativeGapCandidates="20";
 pgn::ui::presentEngineTools(bench.get(),source);if(bench->property("pgn_benchmark_saved_count").toInt())return 4;
 source.raw["pgn_source_time_ns"]="31000000000";source.rowsCompleted="6100";source.raw["pgn_cumulative_tests"]="6025";source.cumulativeGapCandidates="22";
 pgn::ui::presentEngineTools(bench.get(),source);
 auto receipt=bench->property("pgn_benchmark_last_receipt").toJsonObject();
 if(receipt["positions"].toString()!="6000"||receipt["tests"].toString()!="6000"||receipt["retained_gaps"].toString()!="12"||receipt["positions_per_second"].toDouble()!=200||receipt["source_seconds"].toDouble()!=30||receipt["network_published"].toBool()||receipt["controlled_comparison"].toBool())return 5;
 if(bench->property("pgn_benchmark_saved_count").toInt()!=1||!measure->isEnabled()||cancel->isEnabled())return 6;
 measure->click();source.runId="changed-run";source.raw["pgn_source_time_ns"]="62000000000";pgn::ui::presentEngineTools(bench.get(),source);
 if(bench->property("pgn_benchmark_saved_count").toInt()!=1||!bench->property("pgn_benchmark_error").toString().contains("identity"))return 7;
 measure->click();cancel->click();if(cancel->isEnabled()||!measure->isEnabled())return 8;
 FrontierResourceTelemetrySnapshot resources;resources.valid=true;resources.observedUtc=QDateTime::currentDateTimeUtc();FrontierResourceDevice d;d.name="RTX3060";d.deviceId="actual-gpu";d.smClockMhz=1800.;resources.devices.append(d);pgn::ui::presentBenchmarkResources(bench.get(),resources);
 auto* hardware=bench->findChild<QTableWidget*>("pgn-benchmark-hardware");if(hardware->rowCount()!=1||hardware->item(0,1)->text()!="1800"||hardware->item(0,2)->text()!="Unavailable")return 9;
 auto* hardwareCell=hardware->item(0,0);pgn::ui::presentBenchmarkResources(bench.get(),resources);if(hardwareCell!=hardware->item(0,0))return 16;
 const QJsonObject input{{"schema","pgn-source-profile-v1"},{"profile",QJsonObject{{"engine","pgn-native-walk"},{"start",source.domainStart},{"end",source.domainEnd},{"ssh_key","NEVER_IMPORT"},{"command","NOT_EXECUTABLE"}}}};
 const auto path=root.path()+"/input.json";QFile file(path);if(!file.open(QIODevice::WriteOnly))return 10;file.write(QJsonDocument(input).toJson());file.close();
 if(!pgn::ui::importEngineProfile(config.get(),path))return 11;const auto imported=config->property("pgn_engine_imported_profile").toJsonObject();
 if(imported.contains("ssh_key")||imported.contains("command")||config->property("pgn_engine_actual_run").toString()!="actual-run")return 12;
 if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return 13;file.write("{}");file.close();if(pgn::ui::importEngineProfile(config.get(),path))return 13;
 if(bench->minimumSizeHint().height()>600)return 14;
 // Exercise the actual export buttons and file-dialog acceptance. The test
 // uses Qt's picker for deterministic offscreen QA, not a parallel exporter.
 const auto exportThroughDialog=[&](QWidget* view,const QString& path){
  bool accepted=false;
  QTimer::singleShot(0,[&]{if(auto* picker=qobject_cast<QFileDialog*>(app.activeModalWidget())){picker->selectFile(path);accepted=true;QMetaObject::invokeMethod(picker,"accept",Qt::QueuedConnection);}});
  QTimer::singleShot(2000,[&]{if(auto* picker=qobject_cast<QFileDialog*>(app.activeModalWidget()))picker->reject();});
  view->findChild<QPushButton*>("pgn-tools-export")->click();
  return accepted&&QFile::exists(path);
 };
 auto* history=bench->findChild<QTableWidget*>("pgn-benchmark-history");history->selectRow(0);
 const auto benchmarkExport=root.path()+"/benchmark-export.json",profileExport=root.path()+"/profile-export.json";
 if(!exportThroughDialog(bench.get(),benchmarkExport)||!exportThroughDialog(config.get(),profileExport))return 19;
 QFile exported(benchmarkExport);if(!exported.open(QIODevice::ReadOnly))return 20;
 const auto saved=QJsonDocument::fromJson(exported.readAll()).object();
 if(saved!=receipt)return 21;
 QFile exportedProfile(profileExport);if(!exportedProfile.open(QIODevice::ReadOnly))return 22;
 const auto savedProfile=QJsonDocument::fromJson(exportedProfile.readAll()).object();
 if(savedProfile.value("schema")!="pgn-source-profile-v1"||savedProfile.value("applied").toBool(true)||savedProfile.value("profile").toObject().contains("ssh_key"))return 23;
 std::cout<<"PASS actual30s raw source benchmark/save/identity guard/cancel/hardware unknown/import whitelist; no runtime mutation\n";return 0;
}
