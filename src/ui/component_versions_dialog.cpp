#include "app/horizon_user_paths.h"
#include "ui/component_versions_dialog.h"
#include "update/component_catalog.h"
#include <QSignalBlocker>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QColor>
#include <QPushButton>
#include <QSysInfo>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace pgn::ui {
namespace {
const QStringList ids{"ui","core","gpu","cpu"},names{"Interface","Core","GPU engine (local)","CPU engine (local)"};
QString codeDirectory(const QString& path){const QFileInfo info(path);return info.isDir()?info.canonicalFilePath():QString{};}
QLabel* label(const QString& text={}){auto* value=new QLabel(text);value->setTextFormat(Qt::PlainText);value->setWordWrap(true);return value;}
QTableWidget* table(const QStringList& headers){auto* value=new QTableWidget(0,headers.size());value->setHorizontalHeaderLabels(headers);value->verticalHeader()->hide();value->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);value->horizontalHeader()->setStretchLastSection(true);value->horizontalHeader()->setFixedHeight(32);value->setEditTriggers(QAbstractItemView::NoEditTriggers);value->setSelectionBehavior(QAbstractItemView::SelectRows);value->setSelectionMode(QAbstractItemView::SingleSelection);return value;}
void identifyConfiguredCpu(ComponentVersionsContext& context,const QString& productRoot,const QString& app){
 context.codeRoots.remove("cpu");context.primaryPrograms.remove("cpu");
 const auto admit=[&](const QString& program){const QFileInfo file(program);if(file.isAbsolute()&&file.isFile()&&file.isExecutable()){context.codeRoots.insert("cpu",file.canonicalPath());context.primaryPrograms.insert("cpu",file.canonicalFilePath());}};
 // This is the same bounded explicit endpoint profile consumed by main.cpp.
 // Its existence suppresses the fallback. Remote transports never identify
 // a local CPU engine, even if their ssh executable exists on this machine.
 QFile profile(pgn::app::mutableDataRoot(productRoot)+"/testnet/ui-runtime/cpu-controllers.json");
 if(profile.exists()){
  if(!context.testnet||!profile.open(QIODevice::ReadOnly)||profile.size()>32768)return;
  QJsonParseError error;const auto document=QJsonDocument::fromJson(profile.readAll(),&error);const auto object=document.object();const auto endpoints=object.value("endpoints").toArray();
  if(error.error!=QJsonParseError::NoError||!document.isObject()||object.value("schema")!="PGN_CPU_CONTROL_ENDPOINTS_V1"||!object.value("endpoints").isArray()||endpoints.size()>8)return;
  QSet<QString> programs;const auto machine=QSysInfo::machineHostName(),computer=qEnvironmentVariable("COMPUTERNAME");
  for(const auto& value:endpoints){const auto entry=value.toObject();const auto host=entry.value("host").toString().trimmed();const bool local=!host.isEmpty()&&(host.compare(machine,Qt::CaseInsensitive)==0||(!computer.isEmpty()&&host.compare(computer,Qt::CaseInsensitive)==0)||host.compare("localhost",Qt::CaseInsensitive)==0||host=="127.0.0.1"||host=="::1");if(!local)continue;const QFileInfo file(entry.value("program").toString());if(file.isAbsolute()&&file.isFile()&&file.isExecutable())programs.insert(file.canonicalFilePath());}
  if(programs.size()==1)admit(*programs.begin());
  return;
 }
 auto fallback=QDir(app).filePath("EngineRuntime/cpu/horizon-cpu-controller");
#ifdef Q_OS_WIN
 fallback+=".exe";
#endif
 admit(fallback);
}
}
ComponentVersionsContext walletComponentVersionsContext(const QString& productRoot,const QString& runtimeDirectory,bool testnet){
 ComponentVersionsContext context;context.testnet=testnet;const auto app=QCoreApplication::applicationDirPath();context.manifestPath=QDir(app).filePath("components.json");context.codeRoots.insert("ui",codeDirectory(app));context.primaryPrograms.insert("ui",QCoreApplication::applicationFilePath());
 context.applicationRoot=pgn::app::cleanApplicationRoot(productRoot);context.mutableDataRoot=pgn::app::mutableDataRoot(productRoot);
 const auto engine=QDir(app).filePath("EngineRuntime");context.codeRoots.insert("gpu",codeDirectory(engine));identifyConfiguredCpu(context,productRoot,app);
 // Read only the bounded owned configuration receipt. Never enumerate the
 // Core data directory or query remote workers for a guessed code version.
 QFile profile(QDir(runtimeDirectory).filePath("wallet-core.json"));
 if(profile.open(QIODevice::ReadOnly)&&profile.size()<=32768){const auto object=QJsonDocument::fromJson(profile.readAll()).object();context.genesisHash=object.value("genesis_hash").toString();const QFileInfo executable(object.value("executable").toString());if(executable.isAbsolute()&&executable.isFile()&&executable.isExecutable()){context.codeRoots.insert("core",executable.canonicalPath());context.primaryPrograms.insert("core",executable.canonicalFilePath());}}
 return context;
}
QJsonObject inspectLocalComponentVersions(const ComponentVersionsContext& context){
 QJsonObject snapshot{{"schema","PGN_LOCAL_COMPONENT_VIEW_V1"},{"manifest_state","absent"},{"components",QJsonArray{}},{"release_version",QJsonValue{}},{"published",QJsonValue{}}};QJsonObject declarations;QString reason="No local component manifest is present.";
 QFile file(context.manifestPath);
 if(file.exists()){
  snapshot.insert("manifest_state","invalid");reason="The local component manifest is unavailable or invalid.";
  if(file.open(QIODevice::ReadOnly)&&file.size()<=2*1024*1024){QJsonParseError parse;const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);const auto manifest=doc.object();const auto components=manifest.value("components").toArray();bool valid=parse.error==QJsonParseError::NoError&&doc.isObject()&&manifest.value("schema")=="PGN_COMPONENT_MANIFEST_V1"&&components.size()<=4&&manifest.value("components").isArray()&&manifest.value("published").isBool()&&(manifest.value("network")=="testnet"||manifest.value("network")=="main");
   for(const auto& value:components){const auto entry=value.toObject();const auto id=entry.value("id").toString();if(!pgn::update::componentId(id)||declarations.contains(id)){valid=false;break;}declarations.insert(id,entry);}
   if(valid){snapshot.insert("manifest_state","valid");snapshot.insert("published",manifest.value("published"));snapshot.insert("network",manifest.value("network"));if(pgn::update::versionText(manifest.value("release_version").toString()))snapshot.insert("release_version",manifest.value("release_version"));reason="Component has no declared matching local version.";}
   else declarations={};
  }
 }
 QJsonArray rows;
 for(const auto& id:ids){QJsonObject observed{{"id",id},{"state","unidentified"},{"version",QJsonValue{}},{"reason",reason}};const auto root=context.codeRoots.value(id);if(!root.isEmpty()&&declarations.contains(id))observed=pgn::update::inspectComponent(root,declarations.value(id).toObject(),context.primaryPrograms.value(id));else if(root.isEmpty())observed.insert("reason","The local code root is unknown.");observed.insert("code_root",root);rows.append(observed);}
 snapshot.insert("components",rows);return snapshot;
}
ComponentVersionsDialog::ComponentVersionsDialog(ComponentVersionsContext context,QWidget* parent):QDialog(parent),context_(std::move(context)){
 setObjectName("pgn-component-versions");setWindowTitle("Software updates");resize(850,440);
 auto font=QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_WIN
 font.setFamily("Segoe UI");
#endif
 font.setPointSize(10);setFont(font);
 setStyleSheet("QDialog{background:#061219;color:#dcefeb;}QLabel,QTableWidget,QPushButton{color:#dcefeb;}QTableView::indicator{width:14px;height:14px;border:1px solid #5ea596;border-radius:3px;background:#102c35;}QTableView::indicator:checked{background:#71d8bd;}QLabel,QTableWidget,QPushButton,QHeaderView::section{font-family:'"+font.family()+"';font-size:10pt;}QTableWidget{background:#091d25;border:1px solid #28505e;border-radius:8px;gridline-color:#173741;}QHeaderView::section{background:#102c35;color:#9fcacb;border:0;padding:8px;}QPushButton{padding:8px 14px;border:1px solid #2d5c68;border-radius:6px;background:#102c35;}QPushButton:hover{border-color:#5ad9bd;}QPushButton:disabled{color:#65818b;}QPushButton#pgn-components-prepare,QPushButton#pgn-components-install{background:#e5b96b;color:#14202a;font-weight:600;}");
 auto* box=new QVBoxLayout(this);box->setContentsMargins(20,18,20,18);box->setSpacing(12);
 auto* title=label("Keep Horizon up to date");title->setStyleSheet("font-size:18px;font-weight:600;color:#f2f9f7;");box->addWidget(title);
 box->addWidget(label("Choose individual components or select every available update. Nothing installs automatically."));
 localSummary_=label("Checking installed files…");localSummary_->setObjectName("pgn-components-local-summary");box->addWidget(localSummary_);
 components_=table({"Component","Installed","Available","Status"});components_->setObjectName("pgn-components-table");components_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);components_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);components_->setColumnWidth(0,200);components_->verticalHeader()->setDefaultSectionSize(34);components_->setMinimumHeight(177);box->addWidget(components_,1);
 auto* line=new QHBoxLayout;selectAll_=new QPushButton("Select available updates");selectAll_->setObjectName("pgn-components-select-all");refresh_=new QPushButton("Check for updates");refresh_->setObjectName("pgn-components-check");line->addWidget(selectAll_);line->addStretch();line->addWidget(refresh_);box->addLayout(line);
 status_=label("Available versions are checked against signed publisher metadata.");status_->setObjectName("pgn-components-status");status_->setMinimumHeight(40);box->addWidget(status_);
 auto* controls=new QHBoxLayout;auto* close=new QPushButton("Close");controls->addWidget(close);controls->addStretch();cancel_=new QPushButton("Cancel download");cancel_->hide();controls->addWidget(cancel_);
 recover_=new QPushButton("Restore previous version");recover_->setObjectName("pgn-components-recover");recover_->hide();controls->addWidget(recover_);
 prepare_=new QPushButton("Download selected");prepare_->setObjectName("pgn-components-prepare");prepare_->setEnabled(false);controls->addWidget(prepare_);install_=new QPushButton("Restart and install");install_->setObjectName("pgn-components-install");install_->hide();controls->addWidget(install_);box->addLayout(controls);
 connect(close,&QPushButton::clicked,this,&QDialog::close);connect(refresh_,&QPushButton::clicked,this,[this]{refreshLocal();});
 connect(selectAll_,&QPushButton::clicked,this,[this]{for(int row=0;row<components_->rowCount();++row){auto* item=components_->item(row,0);if(item&&(item->flags()&Qt::ItemIsUserCheckable))item->setCheckState(Qt::Checked);}});
 connect(components_,&QTableWidget::itemChanged,this,[this]{if(!busy_&&!installStarted_){preparedRequest_={};install_->hide();prepare_->show();prepare_->setEnabled(!selected().isEmpty());updateRecovery();}});
 connect(prepare_,&QPushButton::clicked,this,[this]{prepareSelected();});connect(install_,&QPushButton::clicked,this,[this]{installPrepared();});connect(cancel_,&QPushButton::clicked,this,[this]{if(service_)service_->cancel();});
 connect(recover_,&QPushButton::clicked,this,[this]{if(busy_||installStarted_||!ensureService())return;preparedRequest_=request("recover",{});installPrepared();});
 resultPoll_=new QTimer(this);resultPoll_->setInterval(500);connect(resultPoll_,&QTimer::timeout,this,[this]{
  QFile file(property("pgn_update_result_file").toString());if(!file.open(QIODevice::ReadOnly)||file.size()>128*1024)return;
  const auto value=QJsonDocument::fromJson(file.readAll()).object();
  if(value.value("schema")=="PGN_UPDATE_HELPER_ERROR_V1"){resultPoll_->stop();installStarted_=false;setBusy(false);preparedRequest_={};install_->hide();prepare_->show();updateRecovery();status_->setText(value.value("error").toString("Installation could not finish. The previous version is retained."));}
  else if(value.value("schema")=="PGN_UPDATE_RECOVERY_RESULT_V1"&&value.value("recovery_performed").toBool()){resultPoll_->stop();installStarted_=false;setBusy(false);preparedRequest_={};install_->hide();refreshLocal();status_->setText("The previous version has been restored and verified.");}
 });
 QTimer::singleShot(0,this,[this]{refreshLocal();});
}
ComponentVersionsDialog::~ComponentVersionsDialog(){closing_=true;if(service_){service_->onResult={};service_->onError={};service_->onStatus={};if(service_->busy())service_->cancel();}}
QStringList ComponentVersionsDialog::selected()const{
 QStringList result;for(int row=0;row<components_->rowCount();++row){const auto* item=components_->item(row,0);if(item&&(item->flags()&Qt::ItemIsUserCheckable)&&item->checkState()==Qt::Checked)result.append(item->data(Qt::UserRole).toString());}return result;
}
void ComponentVersionsDialog::setBusy(bool busy){busy_=busy;refresh_->setEnabled(!busy);components_->setEnabled(!busy);selectAll_->setEnabled(!busy);prepare_->setEnabled(!busy&&!selected().isEmpty());install_->setEnabled(!busy&&!preparedRequest_.isEmpty());recover_->setEnabled(!busy);cancel_->setVisible(busy&&!installStarted_);}
void ComponentVersionsDialog::updateRecovery(){
 const bool needed=!context_.genesisHash.isEmpty()&&pgn::update::componentUpdateNeedsRecovery(context_.genesisHash);
 recover_->setVisible(needed);if(needed){prepare_->setEnabled(false);install_->hide();status_->setText("An interrupted update needs recovery. Restore the previous version before installing another update. Wallets and results stay in their folders.");}
}
void ComponentVersionsDialog::refreshLocal(){
 if(localBusy_||busy_||installStarted_||closing_)return;localBusy_=true;refresh_->setEnabled(false);localSummary_->setText("Checking installed files…");
 auto* watcher=new QFutureWatcher<QJsonObject>(this);localWatcher_=watcher;const QPointer<ComponentVersionsDialog> weak(this);
 connect(watcher,&QFutureWatcher<QJsonObject>::finished,this,[weak,watcher]{if(!weak||weak->closing_)return;weak->localBusy_=false;weak->localWatcher_=nullptr;const auto value=watcher->result();watcher->deleteLater();weak->presentLocal(value);weak->refresh_->setEnabled(true);if(weak->context_.fetchOnOpen)weak->refreshPublished();});
 watcher->setFuture(QtConcurrent::run([context=context_]{return inspectLocalComponentVersions(context);}));
}
void ComponentVersionsDialog::presentLocal(const QJsonObject& snapshot){localSnapshot_=snapshot;if(onLocalSnapshot)onLocalSnapshot(snapshot);localSummary_->setText(snapshot.value("manifest_state")=="valid"?"Installed files checked. Updates apply to this computer.":"Some installed versions could not be verified. Their update controls stay unavailable.");updateRows();updateRecovery();}
void ComponentVersionsDialog::updateRows(){
 const auto chosen=selected();const QSignalBlocker guard(components_);components_->setRowCount(4);
 for(int index=0;index<ids.size();++index){QJsonObject local,available;for(const auto& entry:localSnapshot_.value("components").toArray())if(entry.toObject().value("id")==ids[index])local=entry.toObject();for(const auto& entry:published_.value("components").toArray())if(entry.toObject().value("id")==ids[index])available=entry.toObject();
  const bool known=local.value("state")=="matched_local_manifest";const auto comparison=known?pgn::update::componentVersionComparison(local.value("version").toString(),available.value("version").toString()):std::optional<int>{};
  auto* name=new QTableWidgetItem(names[index]);name->setData(Qt::UserRole,ids[index]);name->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);const bool update=comparison&&*comparison<0;
  if(update){name->setFlags(name->flags()|Qt::ItemIsUserCheckable);name->setCheckState(chosen.isEmpty()||chosen.contains(ids[index])?Qt::Checked:Qt::Unchecked);}
  components_->setItem(index,0,name);components_->setItem(index,1,new QTableWidgetItem(known?local.value("version").toString():"Unverified"));components_->setItem(index,2,new QTableWidgetItem(available.value("version").toString("—")));
  auto* state=new QTableWidgetItem(!known?"Local version unverified":!comparison?"Not verified":*comparison<0?"Update available":*comparison==0?"Up to date":"Newer local build");state->setForeground(update?QColor("#e5b96b"):comparison&&*comparison==0?QColor("#70d8bb"):QColor("#a0b7c0"));state->setToolTip(local.value("reason").toString());components_->setItem(index,3,state);
 }
 prepare_->setEnabled(!busy_&&!selected().isEmpty());
}
QJsonObject ComponentVersionsDialog::request(const QString& action,const QStringList& selected)const{return pgn::update::componentUpdateRequest(deployment_,localSnapshot_,QFileInfo(context_.manifestPath).absolutePath(),context_.mutableDataRoot,action,selected);}
bool ComponentVersionsDialog::ensureService(){
 if(service_)return true;
 try{
  deployment_=pgn::update::componentUpdateDeployment(QFileInfo(context_.manifestPath).absolutePath(),context_.testnet);
  if(!context_.genesisHash.isEmpty()&&context_.genesisHash!=deployment_.context.value("genesis_hash").toString())throw std::runtime_error("Update repository belongs to another Core epoch.");
  service_=new pgn::update::ComponentUpdateService(deployment_,this);service_->onStatus=[this](const QString& text){status_->setText(text);};
  service_->onError=[this](const QString& error){setBusy(false);status_->setText(error);updateRecovery();};
  service_->onResult=[this](const QJsonObject& value){setBusy(false);if(value.value("schema")=="PGN_AUTHENTICATED_COMPONENT_VIEW_V1")presentPublished(value);else if(value.value("schema")=="PGN_UPDATE_INSTALL_PREPARATION_V1"){
   try{preparedRequest_=request("install",selected());preparedRequest_.insert("activation",pgn::update::currentUpdateActivation(context_.applicationRoot,context_.mutableDataRoot));prepare_->hide();install_->show();install_->setEnabled(true);status_->setText("Download verified. Restart when you are ready; mining will pause during installation. Wallets and results keep their current folders.");}
   catch(const std::exception& error){preparedRequest_={};status_->setText(QString::fromUtf8(error.what()));}
  }};return true;
 }catch(const std::exception& error){status_->setText(QString::fromUtf8(error.what()));return false;}
}
void ComponentVersionsDialog::refreshPublished(){if(localBusy_||busy_||installStarted_||!ensureService())return;preparedRequest_={};install_->hide();prepare_->show();setBusy(true);if(!service_->start(request("check",ids)))setBusy(false);}
void ComponentVersionsDialog::presentPublished(const QJsonObject& value,const QString& status){
 if(value.value("schema")!="PGN_AUTHENTICATED_COMPONENT_VIEW_V1"||!value.value("publisher_authenticated").toBool()){status_->setText("Available component versions could not be authenticated.");return;}
 published_=value;if(onPublishedCatalog)onPublishedCatalog(value);updateRows();status_->setText(status.isEmpty()?selected().isEmpty()?"No verified component updates are currently available.":"Select the components to download. Installation remains optional.":status);updateRecovery();
}
void ComponentVersionsDialog::prepareSelected(){if(busy_||recover_->isVisible()||selected().isEmpty()||!ensureService())return;setBusy(true);if(!service_->start(request("prepare_install",selected())))setBusy(false);}
void ComponentVersionsDialog::installPrepared(){
 if(busy_||preparedRequest_.isEmpty()||installStarted_)return;
 try{const auto path=pgn::update::startDetachedComponentInstall(deployment_,preparedRequest_);setProperty("pgn_update_result_file",path);installStarted_=true;setBusy(true);status_->setText(preparedRequest_.value("action")=="recover"?"Restoring the previous version, then restarting Horizon…":"Verifying the installation, then restarting Horizon…");resultPoll_->start();}
 catch(const std::exception& error){status_->setText(QString::fromUtf8(error.what()));}
}
}
