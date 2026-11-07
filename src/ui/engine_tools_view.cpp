#include "app/horizon_user_paths.h"
#include "ui/engine_tools_view.h"
#include "ui/memory_benchmark_view.h"
#include "ui/panel_style.h"
#include "ui/position_formats.h"
#include "telemetry.h"
#include "horizon_coin_navigation.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QUuid>
#include <QTimer>
#include <QRegularExpression>
#include <optional>

namespace pgn::ui { namespace {
QString optionalText(const std::optional<double>& v){return v?QString::number(*v,'g',8):QStringLiteral("Unavailable");}
QTableWidget* table(QWidget* owner,const QStringList& columns){
 auto* t=new QTableWidget(0,columns.size(),owner);t->setHorizontalHeaderLabels(columns);
 t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->setSelectionBehavior(QAbstractItemView::SelectRows);
 t->verticalHeader()->hide();t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
 t->horizontalHeader()->setStretchLastSection(true);t->setAlternatingRowColors(true);return t;
}
void rowHeight(QTableWidget* t,int row,const QStringList& cells){int lines=1;for(const auto& text:cells)lines=std::max(lines,int(text.count('\n'))+1);const int height=std::max(30,lines*t->fontMetrics().lineSpacing()+8);if(t->rowHeight(row)!=height)t->setRowHeight(row,height);}
void row(QTableWidget* t,const QStringList& cells){int n=t->rowCount();t->insertRow(n);for(int i=0;i<cells.size();++i)t->setItem(n,i,new QTableWidgetItem(cells[i]));rowHeight(t,n,cells);}
void stableRows(QTableWidget* t,const QList<QStringList>& rows){
 t->setUpdatesEnabled(false);
 if(t->rowCount()!=rows.size())t->setRowCount(rows.size());
 for(int r=0;r<rows.size();++r)for(int c=0;c<rows[r].size();++c){
  auto* item=t->item(r,c);if(!item)t->setItem(r,c,new QTableWidgetItem(rows[r][c]));
  else if(item->text()!=rows[r][c])item->setText(rows[r][c]);
 }
 for(int r=0;r<rows.size();++r)rowHeight(t,r,rows[r]);
 t->setUpdatesEnabled(true);
}
QString jsonText(const QJsonValue& value){
 if(value.isNull()||value.isUndefined()||(value.isString()&&value.toString().trimmed().isEmpty()))return "Unavailable";
 return value.isString()?value.toString():QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
}
QString positionText(const PositionFormats& value){return value.human=="—"?QString("Unavailable"):value.exact+"\n"+value.stackedText();}
QString profileText(const QString& key,const QJsonObject& profile){
 if(key!="start"&&key!="end")return jsonText(profile.value(key));
 const auto range=positionRangeFormats(profile.value("start").toString(),profile.value("end").toString());
 const auto value=range.valid?(key=="start"?range.start:range.end):positionFormats(profile.value(key).toString());
 return value.human=="—"?jsonText(profile.value(key)):positionText(value);
}
bool save(const QString& path,const QJsonObject& object,QString& error){
 QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}
 const auto bytes=QJsonDocument(object).toJson();if(file.write(bytes)!=bytes.size()||!file.commit()){error=file.errorString();return false;}return true;
}
QJsonObject readSmall(const QString& path,QString& error){
 QFile f(path);if(f.size()>65536){error="File exceeds64KiB bound";return {};}
 if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return {};}
 QJsonParseError parse;const auto doc=QJsonDocument::fromJson(f.readAll(),&parse);
 if(parse.error!=QJsonParseError::NoError||!doc.isObject()){error="Invalid JSON object";return {};}return doc.object();
}
struct Sample {QString run,device,start,end,clock,engine,version,profile,artifact;quint64 ns{},positions{},tests{},retained{};QJsonObject raw;};
std::optional<Sample> counter(const FrontierTelemetrySnapshot& v){
 const auto age=v.observedUtc.msecsTo(QDateTime::currentDateTimeUtc());
 if(!v.authoritative||v.stale||!v.observedUtc.isValid()||age<0||age>5000||
   !v.raw.value("pgn_process_running").toBool())return {};
 bool a=false,b=false,c=false,d=false;
 Sample s{v.runId,v.gpuUuid,v.domainStart,v.domainEnd,v.raw.value("pgn_clock_namespace").toString()};
 s.engine=v.engineId;s.version=v.engineVersion;s.profile=v.engineProfile;s.artifact=v.artifactSha256;
 s.ns=v.raw.value("pgn_source_time_ns").toString().toULongLong(&a);s.positions=v.rowsCompleted.toULongLong(&b);
 s.tests=v.raw.value("pgn_cumulative_tests").toString().toULongLong(&c);s.retained=v.cumulativeGapCandidates.toULongLong(&d);
 if(!a||!b||!c||!d||s.run.isEmpty()||s.device.isEmpty()||s.clock.isEmpty())return {};
 s.raw={{"run",s.run},{"gpu_uuid",s.device},{"start",s.start},{"end",s.end},{"clock_namespace",s.clock},
  {"engine",s.engine},{"version",s.version},{"profile",s.profile},{"engine_artifact_sha256",s.artifact},
  {"source_time_ns",QString::number(s.ns)},{"positions",QString::number(s.positions)},
  {"primality_tests",QString::number(s.tests)},{"retained_gaps",QString::number(s.retained)},
  {"observed_utc",v.observedUtc.toUTC().toString(Qt::ISODateWithMs)}};return s;
}
QJsonObject profile(const FrontierTelemetrySnapshot& v){return {{"engine",v.engineId},{"profile",v.engineProfile},
 {"version",v.engineVersion},{"gpu_uuid",v.gpuUuid},{"gpu_name",v.gpuName},{"start",v.domainStart},{"end",v.domainEnd},
 {"number_bits",v.numberBits?QJsonValue(*v.numberBits):QJsonValue()},{"engine_artifact_sha256",v.artifactSha256},
 {"source_observed_utc",v.observedUtc.toUTC().toString(Qt::ISODateWithMs)}};}
class Tools final:public QWidget {
public:
 Tools(QString root,bool bench,QWidget* parent):QWidget(parent),root_(std::move(root)),bench_(bench){
  setObjectName(bench?"pgn-benchmark-workspace":"pgn-engine-configuration-workspace");setStyleSheet(dataPanelStyle());
  auto* box=new QVBoxLayout(this);box->setContentsMargins(16,10,16,10);box->setSpacing(8);
  auto* title=new QLabel(bench?"BENCHMARK · REAL SOURCE MEASUREMENTS":"ENGINE CONFIGURATION · CURRENT SOURCE");title->setStyleSheet("font:700 18px 'Segoe UI';color:#e9fffb");box->addWidget(title);
  auto* notice=new QLabel(bench?"A declared 30-second source-counter interval. Mining is not restarted. Not a controlled hardware comparison or public community submission.":
   "These settings identify the actual running source. Import/export is a local profile operation: it does not change CUDA, clocks, a reservation or a running job.");notice->setWordWrap(true);box->addWidget(notice);
  status_=new QLabel("Waiting for authoritative source counters");status_->setObjectName("pgn-tools-status");status_->setWordWrap(true);box->addWidget(status_);
  auto* actions=new QHBoxLayout;box->addLayout(actions);
  if(bench){
   measure_=new QPushButton("Measure current run · 30 s");measure_->setObjectName("pgn-benchmark-measure");measure_->setEnabled(false);actions->addWidget(measure_);
   cancel_=new QPushButton("Cancel measurement");cancel_->setObjectName("pgn-benchmark-cancel");cancel_->setEnabled(false);actions->addWidget(cancel_);
   connect(measure_,&QPushButton::clicked,this,[this]{begin();});connect(cancel_,&QPushButton::clicked,this,[this]{finishError("Measurement cancelled. Mining is unchanged.");});
   auto* refresh=new QPushButton("Reload saved measurements");refresh->setObjectName("pgn-benchmark-refresh");actions->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,[this]{history();});
  }else{
   auto* load=new QPushButton("Import profile…");load->setObjectName("pgn-engine-import");actions->addWidget(load);
   connect(load,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Import PGN source profile",{},"PGN profile(*.json)");if(!path.isEmpty())import(path);});
   auto* moduleLoad=new QPushButton("Inspect module capabilities…");moduleLoad->setObjectName("pgn-engine-module-import");actions->addWidget(moduleLoad);
   connect(moduleLoad,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Inspect PGN module capabilities",{},"JSON(*.json)");if(path.isEmpty())return;QString error;const auto input=readSmall(path,error);if(!error.isEmpty())status_->setText("Module inspection rejected: "+error);else module(input,{},{});});
   auto* addModule=new QPushButton("Add module descriptor…");addModule->setObjectName("pgn-engine-module-add");actions->addWidget(addModule);
   connect(addModule,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Add PGN engine module descriptor",{},"JSON(*.json)");if(!path.isEmpty())addModuleFile(path);});
  }
  auto* exportButton=new QPushButton(bench?"Export selected measurement…":"Export current profile…");exportButton->setObjectName("pgn-tools-export");actions->addWidget(exportButton);
  connect(exportButton,&QPushButton::clicked,this,[this]{exportSelected();});
  tabs_=new QTabWidget;tabs_->setObjectName("pgn-engine-tools-tabs");
  source_=table(this,{"Property","Actual source value"});source_->setObjectName("pgn-engine-source");tabs_->addTab(source_,"Source");
  if(bench){hardware_=table(this,{"Device","SM MHz","GPU%","Memory%","Power W","Temperature°C","Raw observed UTC"});hardware_->setObjectName("pgn-benchmark-hardware");tabs_->addTab(hardware_,"Hardware");
   history_=table(this,{"Observed UTC","GPU","Searched positions","Positions/s","Tests/s","Retained gaps","Source seconds","Scope"});history_->setObjectName("pgn-benchmark-history");tabs_->addTab(history_,"Measurements");
   memory_=createMemoryBenchmarkView(root_,this);tabs_->addTab(memory_,"Memory search");
   box->addWidget(HorizonCoinNavigation(tabs_,{{"01 SOURCE","RUN + PARAMETERS"},{"02 HARDWARE","RAW DRIVER"},{"03 MEASUREMENTS","LOCAL EVIDENCE"},{"04 MEMORY SEARCH","CONTROLLED NATIVE WORK"}},this));history();
  }else{imported_=table(this,{"Property","Imported local profile"});imported_->setObjectName("pgn-engine-imported-profile");tabs_->addTab(imported_,"Imported profile");
   module_=table(this,{"Property","Module declaration / observation"});module_->setObjectName("pgn-engine-module-capabilities");tabs_->addTab(module_,"Module capabilities");
   stableRows(module_,{{"status","No module capabilities received"},{"applied","No application evidence received"}});
   auto* catalogue=new QHBoxLayout;moduleSelector_=new QComboBox;moduleSelector_->setObjectName("pgn-engine-module-selector");moduleSelector_->setMinimumContentsLength(18);catalogue->addWidget(moduleSelector_,1);
   auto* reload=new QPushButton("Reload module catalogue");reload->setObjectName("pgn-engine-module-reload");catalogue->addWidget(reload);
   auto* more=new QPushButton("Next page");more->setObjectName("pgn-engine-module-next-page");more->setEnabled(false);catalogue->addWidget(more);
   box->addLayout(catalogue);auto* moduleActions=new QHBoxLayout;
   auto* request=new QPushButton("Request for next job");request->setObjectName("pgn-engine-module-select");moduleActions->addWidget(request);
   auto* receipt=new QPushButton("Inspect last request");receipt->setObjectName("pgn-engine-module-receipt");receipt->setEnabled(false);moduleActions->addWidget(receipt);moduleActions->addStretch();box->addLayout(moduleActions);
   moduleStatus_=new QLabel("Module catalogue not loaded. Current source unchanged.");moduleStatus_->setObjectName("pgn-engine-module-status");moduleStatus_->setWordWrap(true);box->addWidget(moduleStatus_);
   connect(reload,&QPushButton::clicked,this,[this]{moduleCommand({{"action","module_list"}});});
   connect(more,&QPushButton::clicked,this,[this]{if(!moduleNextCursor_.isEmpty())moduleCommand({{"action","module_list"},{"after",moduleNextCursor_}});});
   connect(receipt,&QPushButton::clicked,this,[this]{if(!moduleLastSelection_.isEmpty())moduleCommand({{"action","module_receipt"},{"selection_id",moduleLastSelection_}});});
   connect(request,&QPushButton::clicked,this,[this]{
    if(moduleSelector_->currentData().toString().isEmpty()||latest_.runId.isEmpty()||latest_.gpuUuid.isEmpty()){moduleStatus_->setText("Choose a registered module and wait for the actual source epoch. No request sent.");return;}
    moduleCommand({{"action","module_select"},{"module_key",moduleSelector_->currentData().toString()},{"device_id",latest_.gpuUuid},
      {"owner","LOCAL_UI_OPERATOR"},{"expected_run",latest_.runId}});
   });
   box->addWidget(HorizonCoinNavigation(tabs_,{{"01 CURRENT","ACTUAL ENGINE"},{"02 IMPORTED","LOCAL PROFILE ONLY"},{"03 MODULE","DECLARED CAPABILITIES"}},this));}
  box->addWidget(tabs_,1);
  timeout_=new QTimer(this);timeout_->setSingleShot(true);connect(timeout_,&QTimer::timeout,this,[this]{finishError("No complete30-second source interval arrived within45s. No performance result saved; mining is unchanged.");});
 }
 void device(const FrontierTelemetrySnapshot& v){
  if(memory_)presentMemoryBenchmarkSource(memory_,v);
  latest_=v;const auto p=profile(v);QList<QStringList> profileRows;for(auto i=p.begin();i!=p.end();++i)profileRows.append({i.key(),profileText(i.key(),p)});stableRows(source_,profileRows);
  setProperty("pgn_engine_actual_run",v.runId);setProperty("pgn_engine_profile",p);
  const auto next=counter(v);if(!first_){if(bench_)measure_->setEnabled(bool(next));if(!property("pgn_benchmark_last_receipt").isValid()&&!property("pgn_benchmark_error").isValid())status_->setText(next?"Actual source counters available · mining is unchanged":"Waiting for fresh authoritative source counters");return;}
  if(!next)return;
  if(next->run!=first_->run||next->device!=first_->device||next->start!=first_->start||next->end!=first_->end||next->clock!=first_->clock||next->engine!=first_->engine||next->version!=first_->version||next->profile!=first_->profile||next->artifact!=first_->artifact){finishError("The source identity, engine artifact or range changed during measurement. No result saved; measure the new run separately.");return;}
  if(next->ns<first_->ns){finishError("Source clock reset. No result saved.");return;}
  if(next->ns<=first_->ns)return;
  if(next->positions<first_->positions||next->tests<first_->tests||next->retained<first_->retained){finishError("Source counter reset. No result saved.");return;}
  const auto elapsed=next->ns-first_->ns;status_->setText(QString("Measuring %1 ·%2/30 source seconds · mining unchanged").arg(v.gpuName).arg(double(elapsed)/1e9,0,'f',1));
  if(elapsed<30000000000ULL)return;
  const double seconds=double(elapsed)/1e9;const auto positions=next->positions-first_->positions,tests=next->tests-first_->tests,retained=next->retained-first_->retained;
  QJsonObject receipt{{"schema","pgn-source-window-benchmark-v1"},{"observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
   {"profile",profile(v)},{"begin",first_->raw},{"end",next->raw},{"source_seconds",seconds},{"positions",QString::number(positions)},
   {"tests",QString::number(tests)},{"retained_gaps",QString::number(retained)},{"positions_per_second",double(positions)/seconds},{"tests_per_second",double(tests)/seconds},
   {"hardware_begin",firstHardware_},{"hardware_end",hardwareSnapshot_},{"scope","LOCAL_CURRENT_RUN_SOURCE_WINDOW"},{"controlled_comparison",false},{"network_published",false},{"cpu_mining",false}};
  const QString directory=pgn::app::mutableDataRoot(root_)+"/runtime/benchmarks";QString error;
  if(!QDir().mkpath(directory)||!save(directory+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json",receipt,error)){finishError("Measurement was complete but could not be saved: "+error);return;}
  first_.reset();timeout_->stop();cancel_->setEnabled(false);measure_->setEnabled(true);setProperty("pgn_benchmark_last_receipt",receipt);status_->setText(QString("Saved actual%1s source interval ·%2 positions/s · local only, not published").arg(seconds,0,'f',3).arg(double(positions)/seconds,0,'g',8));history();tabs_->setCurrentWidget(history_);
 }
 void resources(const FrontierResourceTelemetrySnapshot& v){if(!bench_)return;QJsonArray devices;QList<QStringList> hardwareRows;
  for(const auto& d:v.devices){hardwareRows.append({d.name,optionalText(d.smClockMhz),optionalText(d.utilizationPercent),optionalText(d.memoryUtilizationPercent),optionalText(d.powerWatts),optionalText(d.temperatureC),v.observedUtc.toUTC().toString(Qt::ISODateWithMs)});
   devices.append(QJsonObject{{"device",d.deviceId},{"name",d.name},{"sm_mhz",d.smClockMhz?QJsonValue(*d.smClockMhz):QJsonValue()},{"gpu_percent",d.utilizationPercent?QJsonValue(*d.utilizationPercent):QJsonValue()},{"memory_percent",d.memoryUtilizationPercent?QJsonValue(*d.memoryUtilizationPercent):QJsonValue()},{"temperature_c",d.temperatureC?QJsonValue(*d.temperatureC):QJsonValue()},{"power_watts",d.powerWatts?QJsonValue(*d.powerWatts):QJsonValue()},{"throttle_class",d.throttleClass}});}
  stableRows(hardware_,hardwareRows);hardwareSnapshot_={{"valid",v.valid},{"stale",v.stale},{"observed_utc",v.observedUtc.toUTC().toString(Qt::ISODateWithMs)},{"devices",devices}};
 }
 bool import(const QString& path){if(!imported_){status_->setText("Profile import is available in Engine Configuration only.");return false;}QString error;const auto input=readSmall(path,error);
  if(input.value("schema").toString()!="pgn-source-profile-v1"||!input.value("profile").isObject()){status_->setText("Profile import rejected: "+(error.isEmpty()?QString("Expected pgn-source-profile-v1"):error));return false;}
  // Whitelist data fields; imported executable paths, process identities and
  // SSH credentials can never become a runtime command or an applied setting.
  const auto source=input.value("profile").toObject();importedProfile_={};
  for(const auto* key:{"engine","profile","version","gpu_uuid","gpu_name","start","end","number_bits","engine_artifact_sha256","source_observed_utc"})if(source.contains(key))importedProfile_.insert(key,source.value(key));
  imported_->setRowCount(0);for(auto i=importedProfile_.begin();i!=importedProfile_.end();++i)row(imported_,{i.key(),profileText(i.key(),importedProfile_)});
  setProperty("pgn_engine_imported_profile",importedProfile_);status_->setText("Local profile imported for comparison. Running engine and next reservation are unchanged.");tabs_->setCurrentWidget(imported_);return true;
 }
 bool module(const QJsonObject& input,const QString& observedUtc,const QString& artifact){
  if(!module_)return false;
  QString error;
  if(input.value("schema")!="pgn-engine-module-1")error="Expected pgn-engine-module-1";
  for(const auto* key:{"id","protocol"})if(!input.value(key).isString()||input.value(key).toString().trimmed().isEmpty())error=QString("Missing module %1").arg(key);
  for(const auto* key:{"host_refill","host_verification","whole_job_gpu_owned"})if(!input.value(key).isBool())error=QString("Expected boolean %1").arg(key);
  if(input.contains("single_low64_window")&&!input.value("single_low64_window").isBool())error="Expected boolean single_low64_window";
  if(input.contains("supported_high_word")){const auto value=input.value("supported_high_word");bool ok=false;const auto text=value.toString();text.toULongLong(&ok);const auto n=value.toDouble(-1);
   const bool stringValid=value.isString()&&ok&&QRegularExpression("^(0|[1-9][0-9]*)$").match(text).hasMatch();
   const bool numberValid=value.isDouble()&&n>=0&&n<=9007199254740991.&&n==double(quint64(n));
   if(!stringValid&&!numberValid)error="Invalid supported_high_word";}
  if(!error.isEmpty()){setProperty("pgn_engine_module_error",error);status_->setText("Module inspection rejected: "+error);return false;}
  QList<QStringList> rows{{"evidence_scope","Producer declaration only; not qualification or application"},{"observed_utc",observedUtc.isEmpty()?QString("Unavailable"):observedUtc},{"artifact_sha256",artifact.isEmpty()?QString("Unavailable"):artifact},{"applied","No application evidence received"}};
  for(auto i=input.begin();i!=input.end();++i)rows.append({"declared."+i.key(),jsonText(i.value())});stableRows(module_,rows);
  setProperty("pgn_engine_module_capabilities",input);setProperty("pgn_engine_module_observed_utc",observedUtc);setProperty("pgn_engine_module_artifact_sha256",artifact);setProperty("pgn_engine_module_error",QVariant());
  status_->setText("Module declaration inspected. No engine switch requested or applied; running source is unchanged.");tabs_->setCurrentWidget(module_);return true;
 }
 void bindModuleActions(std::function<void(const QJsonObject&)> handler){moduleHandler_=std::move(handler);if(moduleSelector_)moduleCommand({{"action","module_list"}});}
 bool addModuleFile(const QString& path){
  if(!module_||!moduleHandler_)return false;
  QString error;const auto input=readSmall(path,error);
  const auto capabilities=input.value("schema")=="pgn-engine-package-1"?input.value("capabilities").toObject():input;
  if(!error.isEmpty()||!module(capabilities,{},{})){moduleStatus_->setText("Module registration refused: "+(error.isEmpty()?QString("Invalid declaration"):error));return false;}
  moduleCommand({{"action","module_register"},{"package",input},{"source_ref",path}});return true;
 }
 void moduleRegistry(const QJsonObject& packet){
  if(!moduleSelector_)return;
  const auto id=packet.value("request_id").toString();
  const auto action=modulePending_.take(id);
  // The 5s hint retires bounded pending entries, not the last page UUID.
  // A queued native read may finish later; accept only that exact latest
  // page/error, never an unknown UUID or an older page replaced by Reload.
  const bool latestPageReply=!id.isEmpty()&&id==moduleLatestPage_&&
   (packet.value("page").isObject()||!packet.value("error").toString().isEmpty());
  if(action.isEmpty()&&id!="initial-module-catalogue"&&!latestPageReply)return;
  if(packet.contains("page")&&id=="initial-module-catalogue"&&!modulePending_.isEmpty())return;
  if(packet.contains("page")&&!moduleLatestPage_.isEmpty()&&id!=moduleLatestPage_&&id!="initial-module-catalogue")return;
  setProperty("pgn_engine_module_registry",packet);
  if(!packet.value("error").toString().isEmpty()){moduleStatus_->setText("Module catalogue refused: "+packet.value("error").toString()+" · current source unchanged");return;}
  if(packet.contains("page")){
   const auto page=packet.value("page").toObject();moduleSelector_->clear();
   for(const auto value:page.value("items").toArray()){
    const auto item=value.toObject();moduleSelector_->addItem(item.value("module_id").toString()+" · "+item.value("descriptor_sha256").toString().left(8)+" · declaration only",item.value("module_key").toString());
   }
   moduleNextCursor_=page.value("next_cursor").toString();
   findChild<QPushButton*>("pgn-engine-module-next-page")->setEnabled(!moduleNextCursor_.isEmpty());
   setProperty("pgn_engine_module_page_size",moduleSelector_->count());
   setProperty("pgn_engine_module_catalogue_loaded",true);
   const auto last=page.value("latest_request").toObject();
   if(!last.isEmpty()){
    moduleLastSelection_=last.value("request_id").toString();
    findChild<QPushButton*>("pgn-engine-module-receipt")->setEnabled(!moduleLastSelection_.isEmpty());
    setProperty("pgn_engine_module_selection_receipt",last);
   }
   moduleStatus_->setText("Local module declarations loaded. They are not qualified executors or applied switches.");
  }
  if(packet.contains("selection")){
   setProperty("pgn_engine_module_selection_receipt",packet.value("selection").toObject());
   moduleStatus_->setText("Module request saved: controller qualification/admission required. No engine, GPU or reservation was changed.");
  }
 }
private:
 void moduleCommand(QJsonObject command){
  if(!moduleHandler_){if(moduleStatus_)moduleStatus_->setText("Module catalogue service unavailable; current source unchanged");return;}
  if(modulePending_.size()>=16){moduleStatus_->setText("Waiting for module service replies. No additional request sent; current source unchanged.");return;}
  const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces),action=command.value("action").toString();
  command.insert("request_id",id);modulePending_.insert(id,action);
  if(action=="module_select"){
   moduleLastSelection_=id;findChild<QPushButton*>("pgn-engine-module-receipt")->setEnabled(true);
   setProperty("pgn_engine_module_selection_request_id",id);
  }
  if(action=="module_list"||action=="module_register")moduleLatestPage_=id;
  QTimer::singleShot(5000,this,[this,id]{if(!modulePending_.take(id).isEmpty())moduleStatus_->setText("Module request outcome unconfirmed · "+id+" · reload or inspect its receipt before retrying. Current source unchanged.");});
  moduleStatus_->setText("Module request pending · "+id+" · current source unchanged");moduleHandler_(command);
 }
 void begin(){const auto s=counter(latest_);if(!s){status_->setText("No fresh authoritative counters: measurement not started.");return;}setProperty("pgn_benchmark_error",QVariant());first_=s;firstHardware_=hardwareSnapshot_;measure_->setEnabled(false);cancel_->setEnabled(true);timeout_->start(45000);status_->setText("Measuring a30-second source interval. The GPU keeps its current job.");}
 void finishError(const QString& error){first_.reset();timeout_->stop();if(bench_){cancel_->setEnabled(false);measure_->setEnabled(bool(counter(latest_)));}status_->setText(error);setProperty("pgn_benchmark_error",error);}
 void history(){if(!bench_)return;history_->setRowCount(0);receipts_={};QDir directory(pgn::app::mutableDataRoot(root_)+"/runtime/benchmarks");const auto files=directory.entryInfoList({"*.json"},QDir::Files,QDir::Time);
  for(int n=0;n<std::min(int(files.size()),200);++n){QString error;const auto r=readSmall(files[n].absoluteFilePath(),error);if(r.value("schema")!="pgn-source-window-benchmark-v1")continue;receipts_.append(r);row(history_,{jsonText(r.value("observed_utc")),jsonText(r.value("profile").toObject().value("gpu_name")),positionText(positionFormats(r.value("positions").toString())),jsonText(r.value("positions_per_second")),jsonText(r.value("tests_per_second")),jsonText(r.value("retained_gaps")),jsonText(r.value("source_seconds")),"Local current-run window · not public"});}
  setProperty("pgn_benchmark_saved_count",receipts_.size());
 }
 void exportSelected(){QJsonObject object;if(bench_){const int selected=history_->currentRow();if(selected<0||selected>=receipts_.size()){status_->setText("Select a saved measurement to export.");return;}object=receipts_[selected].toObject();}
  else{if(latest_.runId.isEmpty()){status_->setText("No source profile available to export.");return;}object={{"schema","pgn-source-profile-v1"},{"profile",profile(latest_)},{"applied",false}};}
  const QString path=QFileDialog::getSaveFileName(this,"Export exact PGN source evidence",{},"JSON(*.json)");if(path.isEmpty())return;QString error;if(!save(path,object,error)){status_->setText("Export failed: "+error);return;}status_->setText("Export saved. Runtime and mining are unchanged.");
 }
 QString root_;bool bench_;QLabel* status_{};QTableWidget *source_{},*hardware_{},*history_{},*imported_{},*module_{};QTabWidget* tabs_{};QWidget* memory_{};QPushButton *measure_{},*cancel_{};QTimer* timeout_{};
 FrontierTelemetrySnapshot latest_;std::optional<Sample> first_;QJsonObject hardwareSnapshot_,firstHardware_,importedProfile_;QJsonArray receipts_;
 QComboBox* moduleSelector_{};QLabel* moduleStatus_{};QString moduleNextCursor_,moduleLatestPage_,moduleLastSelection_;QHash<QString,QString> modulePending_;std::function<void(const QJsonObject&)> moduleHandler_;
};
}
QWidget* createBenchmarkView(const QString& root,QWidget* p){return new Tools(root,true,p);}
QWidget* createEngineConfigurationView(const QString& root,QWidget* p){return new Tools(root,false,p);}
void presentEngineTools(QWidget* w,const FrontierTelemetrySnapshot& v){if(auto* t=dynamic_cast<Tools*>(w))t->device(v);}
void presentBenchmarkResources(QWidget* w,const FrontierResourceTelemetrySnapshot& v){if(auto* t=dynamic_cast<Tools*>(w))t->resources(v);}
bool importEngineProfile(QWidget* w,const QString& path){if(auto* t=dynamic_cast<Tools*>(w))return t->import(path);return false;}
bool presentEngineModuleCapabilities(QWidget* w,const QJsonObject& capabilities,const QString& observedUtc,const QString& artifact){if(auto* t=dynamic_cast<Tools*>(w))return t->module(capabilities,observedUtc,artifact);return false;}
void bindEngineModuleActions(QWidget* w,std::function<void(const QJsonObject&)> handler){if(auto* t=dynamic_cast<Tools*>(w))t->bindModuleActions(std::move(handler));}
bool addEngineModuleDescriptor(QWidget* w,const QString& path){if(auto* t=dynamic_cast<Tools*>(w))return t->addModuleFile(path);return false;}
void presentEngineModuleRegistry(QWidget* w,const QJsonObject& receipt){if(auto* t=dynamic_cast<Tools*>(w))t->moduleRegistry(receipt);}
}
