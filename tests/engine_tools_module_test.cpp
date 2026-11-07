#include "qt_offscreen_fixture.h"
#include "ui/engine_tools_view.h"
#include "telemetry.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileDialog>
#include <QTimer>
#include <QEventLoop>
#include <QPluginLoader>
#include <QFileInfo>
#include <iostream>
#include <memory>
static int checks{};
#define REQUIRE(x) do { ++checks;if(!(x)){std::cerr<<"FAIL checks="<<checks<<" line "<<__LINE__<<": "<<#x<<'\n';return 1;} } while(0)
static QString cell(QTableWidget* t,const QString& key){for(int r=0;r<t->rowCount();++r)if(t->item(r,0)->text()==key)return t->item(r,1)->text();return {};}
static FrontierTelemetrySnapshot source(){FrontierTelemetrySnapshot v;v.authoritative=true;v.observedUtc=QDateTime::currentDateTimeUtc();v.runId="fixture-run";v.gpuUuid="fixture-gpu";v.engineId="fixture-engine";v.artifactSha256="fixture-artifact-A";v.domainStart="184467440737095516160";v.domainEnd="184467440737095516460";v.rowsCompleted="0";v.cumulativeGapCandidates="0";v.raw={{"pgn_process_running",true},{"pgn_clock_namespace","fixture-clock"},{"pgn_source_time_ns","1000000000"},{"pgn_cumulative_tests","0"}};return v;}
static int lateCatalogue(const QString& root){
 std::unique_ptr<QWidget> view(pgn::ui::createEngineConfigurationView(root));QJsonArray sent;
 pgn::ui::bindEngineModuleActions(view.get(),[&](const QJsonObject& command){sent.append(command);});
 REQUIRE(sent.size()==1);const auto first=sent.first().toObject().value("request_id").toString();
 auto* status=view->findChild<QLabel*>("pgn-engine-module-status");auto* selector=view->findChild<QComboBox*>("pgn-engine-module-selector");
 REQUIRE(status&&selector);REQUIRE(!view->isVisible());REQUIRE(!view->property("pgn_engine_module_catalogue_loaded").isValid());
 // Exercise the REAL unchanged production 5000ms timer. Poll only this new
 // hidden fixture's status; no retry, cold runtime/SSH or UI existing elsewhere.
 QEventLoop loop;QTimer poll,safety;poll.setInterval(10);safety.setSingleShot(true);
 QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(status->text().contains("outcome unconfirmed")&&status->text().contains(first))loop.quit();});
 QObject::connect(&safety,&QTimer::timeout,&loop,&QEventLoop::quit);
 poll.start();safety.start(7500);loop.exec();poll.stop();safety.stop();
 REQUIRE(status->text().contains("outcome unconfirmed")&&status->text().contains(first));REQUIRE(sent.size()==1);
 const QString key(64,'a');const QJsonObject page{{"items",QJsonArray{QJsonObject{{"module_key",key},{"module_id","PUBLIC-LATE-FIXTURE"},{"descriptor_sha256",QString(64,'b')}}}}};
 pgn::ui::presentEngineModuleRegistry(view.get(),{{"type","module_registry"},{"request_id",first},{"page",page}});
 REQUIRE(view->property("pgn_engine_module_catalogue_loaded")==true);REQUIRE(selector->count()==1);
 REQUIRE(view->property("pgn_engine_module_registry").toJsonObject().value("request_id")==first);
 const auto before=view->property("pgn_engine_module_registry");const auto beforeStatus=status->text();
 pgn::ui::presentEngineModuleRegistry(view.get(),{{"type","module_registry"},{"request_id","unknown-reply"},{"page",QJsonObject{{"items",QJsonArray{}}}}});
 REQUIRE(view->property("pgn_engine_module_registry")==before&&status->text()==beforeStatus&&selector->count()==1);
 auto* reload=view->findChild<QPushButton*>("pgn-engine-module-reload");REQUIRE(reload);reload->click();const auto second=sent.last().toObject().value("request_id").toString();
 REQUIRE(sent.size()==2&&second!=first);const auto pendingStatus=status->text();
 pgn::ui::presentEngineModuleRegistry(view.get(),{{"type","module_registry"},{"request_id",first},{"page",QJsonObject{{"items",QJsonArray{}}}}});
 REQUIRE(view->property("pgn_engine_module_registry")==before&&status->text()==pendingStatus&&selector->count()==1);
 pgn::ui::presentEngineModuleRegistry(view.get(),{{"type","module_registry"},{"request_id",second},{"page",QJsonObject{{"items",QJsonArray{}}}}});
 REQUIRE(selector->count()==0&&view->property("pgn_engine_module_registry").toJsonObject().value("request_id")==second);
 REQUIRE(sent.size()==2);REQUIRE(!view->property("pgn_engine_applied_module").isValid());
 return 0;
}
int main(int argc,char** argv){
 if(!pgn::test::prepareOffscreen(argc>1?QString::fromLocal8Bit(argv[1]):QString{}))return 80;
 QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);if(QGuiApplication::platformName()!="offscreen")return 83;
 QTemporaryDir temp;REQUIRE(temp.isValid());
 const QString mode=argc>2?QString::fromLocal8Bit(argv[2]):QString{};
 if(mode!="--existing-only"){
  if(const int result=lateCatalogue(temp.path()))return result;
  if(mode=="--catalogue-timeout-only"){std::cout<<"PASS checks="<<checks<<" failures=0; actual 5000ms expiry, exact late page accepted, unknown/superseded UUID rejected, no retry; own hidden OFFSCREEN only\n";return 0;}
 }
 std::unique_ptr<QWidget> config(pgn::ui::createEngineConfigurationView(temp.path())),bench(pgn::ui::createBenchmarkView(temp.path()));
 const QJsonObject caps{{"schema","pgn-engine-module-1"},{"id","fixture-module"},{"protocol","phase0-source-1"},{"host_refill",false},{"host_verification",false},{"whole_job_gpu_owned",true},{"single_low64_window",true},{"supported_high_word",10},{"extension",QJsonObject{{"zero",0},{"missing",QJsonValue()}}}};
 const QString timestamp="2026-10-12T20:01:02.1234567Z";
 auto v=source();pgn::ui::presentEngineTools(config.get(),v);
 QJsonArray commands;pgn::ui::bindEngineModuleActions(config.get(),[&](const QJsonObject& command){commands.append(command);});
 REQUIRE(commands.size()==1);REQUIRE(commands[0].toObject().value("action")=="module_list");
 const QString moduleKey(64,QChar('a'));
 const auto page=QJsonObject{{"items",QJsonArray{QJsonObject{{"module_key",moduleKey},{"module_id","fixture-module"},{"descriptor_sha256",QString(64,QChar('b'))}}}},{"next_cursor",moduleKey}};
 pgn::ui::presentEngineModuleRegistry(config.get(),{{"type","module_registry"},{"request_id",commands[0].toObject().value("request_id")},{"page",page}});
 REQUIRE(config->findChild<QComboBox*>("pgn-engine-module-selector")->count()==1);
 REQUIRE(config->findChild<QPushButton*>("pgn-engine-module-next-page")->isEnabled());
 config->findChild<QPushButton*>("pgn-engine-module-select")->click();const auto selection=commands.last().toObject();
 REQUIRE(selection.value("action")=="module_select");REQUIRE(selection.value("module_key")==moduleKey);
 REQUIRE(selection.value("expected_run")==v.runId);REQUIRE(selection.value("device_id")==v.gpuUuid);
 REQUIRE(!selection.contains("start"));REQUIRE(!selection.contains("booking_id"));
 const QJsonObject selectionReceipt{{"state","REQUESTED_NOT_ADMITTED"},{"applied",false},{"request_id",selection.value("request_id")}};
 pgn::ui::presentEngineModuleRegistry(config.get(),{{"type","module_registry"},{"request_id",selection.value("request_id")},{"selection",selectionReceipt}});
 auto* moduleStatus=config->findChild<QLabel*>("pgn-engine-module-status");REQUIRE(moduleStatus);REQUIRE(moduleStatus->text().contains("No engine, GPU or reservation"));
 pgn::ui::presentEngineTools(config.get(),v);REQUIRE(moduleStatus->text().contains("No engine, GPU or reservation"));
 config->findChild<QPushButton*>("pgn-engine-module-receipt")->click();REQUIRE(commands.last().toObject().value("selection_id")==selection.value("request_id"));
 pgn::ui::presentEngineModuleRegistry(config.get(),{{"request_id",commands.last().toObject().value("request_id")},{"selection",selectionReceipt}});
 pgn::ui::presentEngineModuleRegistry(config.get(),{{"request_id","unknown-reply"},{"page",QJsonObject{{"items",QJsonArray{}}}}});
 REQUIRE(config->findChild<QComboBox*>("pgn-engine-module-selector")->count()==1);
 config->findChild<QPushButton*>("pgn-engine-module-next-page")->click();REQUIRE(commands.last().toObject().value("after")==moduleKey);
 config->findChild<QPushButton*>("pgn-engine-module-reload")->click();const auto reload=commands.last().toObject();
 pgn::ui::presentEngineModuleRegistry(config.get(),{{"request_id",commands[commands.size()-2].toObject().value("request_id")},{"page",QJsonObject{{"items",QJsonArray{}}}}});
 REQUIRE(config->findChild<QComboBox*>("pgn-engine-module-selector")->count()==1); // An older page cannot displace a newer request.
 pgn::ui::presentEngineModuleRegistry(config.get(),{{"request_id",reload.value("request_id")},{"page",page}});
 const QString packagePath=temp.path()+"/module-package.json";QFile packageFile(packagePath);REQUIRE(packageFile.open(QIODevice::WriteOnly));
 const QJsonObject package{{"schema","pgn-engine-package-1"},{"capabilities",caps},{"artifact_ref","C:/user-engine/bin/source.exe"},{"artifact_sha256",QString(64,QChar('c'))}};
 packageFile.write(QJsonDocument(package).toJson());packageFile.close();
 REQUIRE(pgn::ui::addEngineModuleDescriptor(config.get(),packagePath));REQUIRE(commands.last().toObject().value("package").toObject()==package);
 REQUIRE(commands.last().toObject().value("source_ref")==packagePath);REQUIRE(!pgn::ui::addEngineModuleDescriptor(bench.get(),packagePath));
 const auto restoredPage=QJsonObject{{"items",page.value("items")},{"latest_request",selectionReceipt}};
 pgn::ui::presentEngineModuleRegistry(config.get(),{{"request_id",commands.last().toObject().value("request_id")},{"page",restoredPage}});
 REQUIRE(!config->findChild<QPushButton*>("pgn-engine-module-next-page")->isEnabled());
 REQUIRE(config->property("pgn_engine_module_selection_receipt").toJsonObject()==selectionReceipt);
 REQUIRE(pgn::ui::presentEngineModuleCapabilities(config.get(),caps,timestamp,"fixture-sha"));
 auto* module=config->findChild<QTableWidget*>("pgn-engine-module-capabilities");REQUIRE(module);
 REQUIRE(cell(module,"declared.host_refill")=="false");REQUIRE(cell(module,"observed_utc")==timestamp);
 REQUIRE(cell(module,"artifact_sha256")=="fixture-sha");REQUIRE(cell(module,"applied")=="No application evidence received");
 REQUIRE(config->property("pgn_engine_actual_run").toString()==v.runId);
 REQUIRE(!config->property("pgn_engine_applied_module").isValid());
 auto invalid=caps;invalid.remove("host_refill");REQUIRE(!pgn::ui::presentEngineModuleCapabilities(config.get(),invalid));
 REQUIRE(config->property("pgn_engine_module_capabilities").toJsonObject()==caps);
 for(const QJsonValue& bad:QJsonArray{QString("01"),QString("+10"),QString(" 10"),-1.,1.5,9007199254740992.}){invalid=caps;invalid["supported_high_word"]=bad;REQUIRE(!pgn::ui::presentEngineModuleCapabilities(config.get(),invalid));}
 REQUIRE(!pgn::ui::presentEngineModuleCapabilities(bench.get(),caps));
 REQUIRE(pgn::ui::presentEngineModuleCapabilities(config.get(),caps));REQUIRE(cell(module,"observed_utc")=="Unavailable");REQUIRE(cell(module,"artifact_sha256")=="Unavailable");
 auto future=caps;future["protocol"]="future-protocol-2";future["supported_high_word"]="18446744073709551615";
 REQUIRE(pgn::ui::presentEngineModuleCapabilities(config.get(),future));REQUIRE(cell(module,"declared.protocol")=="future-protocol-2");
 // Exercise the actual capability inspection button with Qt's local picker.
 const QString path=temp.path()+"/capabilities.json";QFile file(path);REQUIRE(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(caps).toJson());file.close();
 bool accepted=false;QTimer::singleShot(0,[&]{if(auto* picker=qobject_cast<QFileDialog*>(app.activeModalWidget())){picker->selectFile(path);accepted=true;QMetaObject::invokeMethod(picker,"accept",Qt::QueuedConnection);}});
 QTimer::singleShot(2000,[&]{if(auto* picker=qobject_cast<QFileDialog*>(app.activeModalWidget()))picker->reject();});
 config->findChild<QPushButton*>("pgn-engine-module-import")->click();REQUIRE(accepted);REQUIRE(config->property("pgn_engine_module_capabilities").toJsonObject()==caps);
 // Null profile values must stay unknown instead of becoming numeric zero.
 REQUIRE(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(QJsonObject{{"schema","pgn-source-profile-v1"},{"profile",QJsonObject{{"number_bits",QJsonValue()},{"version",false},{"start","0"}}}}).toJson());file.close();
 REQUIRE(pgn::ui::importEngineProfile(config.get(),path));REQUIRE(!pgn::ui::importEngineProfile(bench.get(),path));
 const auto tables=config->findChildren<QTableWidget*>();QTableWidget* imported=nullptr;for(auto* t:tables)if(t!=module&&t->horizontalHeaderItem(1)->text()=="Imported local profile")imported=t;
 REQUIRE(imported);REQUIRE(cell(imported,"number_bits")=="Unavailable");REQUIRE(cell(imported,"version")=="false");REQUIRE(cell(imported,"start")=="0\n0\n0e0\n0");
 REQUIRE(config->property("pgn_engine_imported_profile").toJsonObject().value("start")=="0");
 auto* measure=bench->findChild<QPushButton*>("pgn-benchmark-measure");
 v.observedUtc=QDateTime::currentDateTimeUtc().addSecs(1);pgn::ui::presentEngineTools(bench.get(),v);REQUIRE(!measure->isEnabled());
 v=source();pgn::ui::presentEngineTools(bench.get(),v);REQUIRE(measure->isEnabled());measure->click();
 v.artifactSha256="fixture-artifact-B";v.raw["pgn_source_time_ns"]="31000000000";pgn::ui::presentEngineTools(bench.get(),v);
 REQUIRE(bench->property("pgn_benchmark_error").toString().contains("artifact"));REQUIRE(bench->property("pgn_benchmark_saved_count").toInt()==0);
 v=source();pgn::ui::presentEngineTools(bench.get(),v);measure->click();REQUIRE(!bench->property("pgn_benchmark_error").isValid());
 v.raw["pgn_source_time_ns"]="0";pgn::ui::presentEngineTools(bench.get(),v);REQUIRE(bench->property("pgn_benchmark_error").toString().contains("clock reset"));
 // Zero counters are real measurements; missing hardware stays null in receipt.
 v=source();pgn::ui::presentEngineTools(bench.get(),v);FrontierResourceTelemetrySnapshot resources;resources.valid=true;resources.observedUtc=QDateTime::currentDateTimeUtc();FrontierResourceDevice d;d.deviceId="fixture-gpu";d.temperatureC=0.;resources.devices.append(d);pgn::ui::presentBenchmarkResources(bench.get(),resources);
 measure->click();v.raw["pgn_source_time_ns"]="31000000000";pgn::ui::presentEngineTools(bench.get(),v);
 const auto receipt=bench->property("pgn_benchmark_last_receipt").toJsonObject();REQUIRE(receipt.value("positions")=="0");REQUIRE(receipt.value("positions_per_second").toDouble(-1)==0.);
 const auto hardware=receipt.value("hardware_end").toObject().value("devices").toArray()[0].toObject();REQUIRE(hardware.value("temperature_c").toDouble(-1)==0.);REQUIRE(hardware.value("memory_percent").isNull());
 REQUIRE(receipt.value("network_published")==false);
 // Incomplete saved records stay visibly incomplete, not zero-rate successes.
 QFile incomplete(temp.path()+"/data/runtime/benchmarks/incomplete.json");REQUIRE(incomplete.open(QIODevice::WriteOnly));incomplete.write(QJsonDocument(QJsonObject{{"schema","pgn-source-window-benchmark-v1"}}).toJson());incomplete.close();
 bench->findChild<QPushButton*>("pgn-benchmark-refresh")->click();auto* history=bench->findChild<QTableWidget*>("pgn-benchmark-history");bool missing=false;
 for(int r=0;r<history->rowCount();++r)if(history->item(r,0)->text()=="Unavailable"){
  missing=true;for(const auto& name:QStringList{"Searched positions","Positions/s","Tests/s","Retained gaps","Source seconds"}){
   int column=-1;for(int c=0;c<history->columnCount();++c)if(history->horizontalHeaderItem(c)->text()==name)column=c;
   REQUIRE(column>=0);REQUIRE(history->item(r,column)->text()=="Unavailable");
  }
 }REQUIRE(missing);
 std::cout<<"PASS checks="<<checks<<" failures=0; closed offscreen module catalogue/import, selection correlation/stable status/stale-page guards, evidence separation, raw absence/zero, benchmark artifact/clock guards; no physical GPU claim\n";
}
