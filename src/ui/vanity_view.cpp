// MIT. Genuine local Core key custody and explicitly selected public-only workers.
#include "ui/vanity_view.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QProcess>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QSet>
#include <memory>
namespace pgn::ui {
namespace {
class VanityView final:public QWidget {
 struct Device {QCheckBox* box;QString host,mask;};
 struct Worker {QProcess* process{};QString host,id;QByteArray pending;bool finished{};quint64 hashed{};qint64 elapsed{};double rate{};};
 QString root_,directory_,key_,helper_,remote_,sourceRun_;QJsonObject profile_,job_,winner_;QList<Device> devices_;QList<std::shared_ptr<Worker>> workers_;
 QLineEdit* prefix_{};QLineEdit* address_{};QLabel* state_{};QVBoxLayout* choices_{};QPushButton* generate_{};QPushButton* check_{};QPushButton* cancel_{};QCheckBox* gpu_{};
 QSet<QString> cancelling_;bool busy_{},cancelled_{},resume_{},finalizing_{},polling_{};int waitCount_{};
 using Done=std::function<void(QJsonObject)>;
public:
 explicit VanityView(QString root):root_(std::move(root)){
  setObjectName("pgn-vanity-generator");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(28,20,28,20);layout->setSpacing(12);
  auto* title=new QLabel("VANITY ADDRESSES");title->setStyleSheet("font:700 24px 'Segoe UI';color:#69efc5");layout->addWidget(title);
  auto* intro=new QLabel("Choose a PGN prefix and the processors to use. Private keys stay encrypted on this PC; workers search public points only.");intro->setWordWrap(true);layout->addWidget(intro);
  auto* row=new QHBoxLayout;prefix_=new QLineEdit;prefix_->setObjectName("pgn-vanity-prefix");prefix_->setText("PGNWizz");prefix_->setMaxLength(35);row->addWidget(prefix_,1);check_=new QPushButton("Check prefix");check_->setObjectName("pgn-vanity-check");row->addWidget(check_);layout->addLayout(row);
  auto* alphabet=new QLabel("Base58 is case sensitive. Characters 0, O, I and lowercase l are unavailable.");alphabet->setWordWrap(true);layout->addWidget(alphabet);
  choices_=new QVBoxLayout;layout->addLayout(choices_);gpu_=new QCheckBox("WIZZ · NVIDIA RTX 3090 · GPU");gpu_->setObjectName("pgn-vanity-gpu-wizz-3090");gpu_->setChecked(true);choices_->addWidget(gpu_);
  auto* actions=new QHBoxLayout;generate_=new QPushButton("Generate with selected devices");generate_->setObjectName("pgn-vanity-generate");cancel_=new QPushButton("Cancel search");cancel_->setObjectName("pgn-vanity-cancel");cancel_->setEnabled(false);actions->addWidget(generate_);actions->addWidget(cancel_);actions->addStretch();layout->addLayout(actions);
  state_=new QLabel("Loading actual CPU packages…");state_->setObjectName("pgn-vanity-status");state_->setWordWrap(true);layout->addWidget(state_);
  address_=new QLineEdit;address_->setReadOnly(true);address_->setObjectName("pgn-vanity-address");address_->setPlaceholderText("Generated address");layout->addWidget(address_);
  auto* scope=new QLabel("Fixed PGN Base58Check format · exact Core ownership verification · Windows account protected key. Keep the original session files for recovery.");scope->setWordWrap(true);scope->setStyleSheet("color:#87ada9");layout->addWidget(scope);layout->addStretch();
  connect(check_,&QPushButton::clicked,this,[this]{if(!load())return;local({"--plan",prefix_->text()},[this](const auto& o){state_->setText(o.value("status")=="pattern_ready"?"Prefix supported by the fixed PGN network format.":o.value("error").toString());});});
  connect(generate_,&QPushButton::clicked,this,[this]{begin();});connect(cancel_,&QPushButton::clicked,this,[this]{cancelled_=true;state_->setText("Cancelling selected workers safely…");stopWorkers();if(workers_.isEmpty()&&!polling_)finish();});
  auto* cancellationClock=new QTimer(this);cancellationClock->setInterval(1500);connect(cancellationClock,&QTimer::timeout,this,[this]{if(busy_&&(cancelled_||!winner_.isEmpty()))stopWorkers();});cancellationClock->start();
  QTimer::singleShot(0,this,[this]{inventory();});
 }
 ~VanityView()override{for(const auto& w:workers_)if(!w->finished&&w->host=="local"){QFile f(directory_+"/cpu.cancel");if(f.open(QIODevice::WriteOnly))f.write("stop\n");}}
private:
 void busy(bool b){busy_=b;check_->setEnabled(!b);generate_->setEnabled(!b);prefix_->setEnabled(!b);gpu_->setEnabled(!b);for(const auto& d:devices_)d.box->setEnabled(!b);cancel_->setEnabled(b);setProperty("pgn_vanity_busy",b);setProperty("pgn_vanity_resume_pending",resume_);}
 bool load(){
  QFile spec(root_+"/data/vanity-runtime.json");if(!spec.open(QIODevice::ReadOnly)||spec.size()>8192){state_->setText("Generator deployment unavailable.");return false;}profile_=QJsonDocument::fromJson(spec.readAll()).object();
  helper_=QFileInfo(profile_.value("executable").toString()).canonicalFilePath();const auto base=QFileInfo(root_+"/wallet/native-runtime").canonicalFilePath();QFile exe(helper_);
  if(profile_.value("schema")!="pgn-vanity-runtime-1"||base.isEmpty()||!helper_.startsWith(base+"/",Qt::CaseInsensitive)||QFileInfo(helper_).fileName()!="pgn-vanity-session.exe"||!exe.open(QIODevice::ReadOnly)||exe.size()>67108864||QString::fromLatin1(QCryptographicHash::hash(exe.readAll(),QCryptographicHash::Sha256).toHex())!=profile_.value("sha256").toString()){state_->setText("Native generator fingerprint unavailable or mismatched.");return false;}
  remote_=profile_.value("remote_executable").toString();if(!QRegularExpression("^C:/PGN/native/vanity-[0-9-]+/pgn-vanity-session[.]exe$").match(remote_).hasMatch()){state_->setText("Versioned remote generator unavailable.");return false;}
  QFile config(root_+"/evidence/native-transport-recovered-20261012.json");if(!config.open(QIODevice::ReadOnly)||config.size()>8192)return false;const auto host=QJsonDocument::fromJson(config.readAll()).object().value("research").toObject().value("cuda").toObject();if(host.value("host")!="wizz@wizz")return false;key_=host.value("key").toString();return !key_.isEmpty();
 }
 QProcess* execute(const QString& program,const QStringList& args,const QByteArray& input,Done done,int timeout=20000){
  auto* p=new QProcess(this);auto timer=new QTimer(p);timer->setSingleShot(true);p->setProgram(program);p->setArguments(args);p->setProperty("pgn_done",false);
  connect(timer,&QTimer::timeout,p,[p]{p->setProperty("pgn_timeout",true);p->kill();});
  connect(p,&QProcess::started,p,[p,input]{if(!input.isEmpty())p->write(input+"\n");p->closeWriteChannel();});
  auto complete=[this,p,timer,done=std::move(done)]{if(p->property("pgn_done").toBool())return;p->setProperty("pgn_done",true);timer->stop();const auto bytes=p->readAllStandardOutput();auto o=bytes.size()<=32768?QJsonDocument::fromJson(bytes.trimmed()).object():QJsonObject{};if(o.isEmpty())o={{"status","error"},{"error",p->property("pgn_timeout").toBool()?"Device operation timed out.":"Device returned no valid bounded result."}};p->deleteLater();done(o);};
  connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[complete](int,QProcess::ExitStatus){complete();});connect(p,&QProcess::errorOccurred,this,[p,complete](auto){if(p->state()==QProcess::NotRunning)complete();});p->start();timer->start(timeout);return p;
 }
 void local(const QStringList& a,Done done,const QByteArray& input={}){execute(helper_,a,input,std::move(done));}
 QStringList sshArgs(const QString& command)const{return {"-o","BatchMode=yes","-o","StrictHostKeyChecking=yes","-o","ConnectTimeout=8","-i",key_,"wizz@wizz",command};}
 QString ssh()const{return qEnvironmentVariable("SystemRoot","C:/Windows")+"/System32/OpenSSH/ssh.exe";}
 void remote(const QString& cmd,Done done){execute(ssh(),sshArgs(cmd),{},std::move(done));}
 void control(const QString& script,Done done){const auto bytes=reinterpret_cast<const char*>(script.utf16());const auto encoded=QByteArray(bytes,script.size()*2).toBase64();remote("powershell -NoProfile -NonInteractive -EncodedCommand "+QString::fromLatin1(encoded),std::move(done));}
 void snapshot(Done done){control("& 'C:/PGN/runtime/native-control.ps1' -Action snapshot -NoDriver | ConvertFrom-Json | Select-Object state,owned,has_checkpoint,operator_stop,manifest | ConvertTo-Json -Compress -Depth 6",std::move(done));}
 void inventory(){if(!load())return;local({"--cpu-inventory"},[this](const auto& o){addCpus("local","ALEX",o);});remote(remote_+" --cpu-inventory",[this](const auto& o){addCpus("remote","WIZZ",o);state_->setText("Select one or more devices. Busy selected GPUs can be suspended and resumed automatically.");});}
 void addCpus(const QString& host,const QString& label,const QJsonObject& o){for(const auto& value:o.value("packages").toArray()){const auto v=value.toObject();const auto mask=v.value("mask").toString();if(!QRegularExpression("^[0-9a-f]{1,16}$").match(mask).hasMatch())continue;auto* box=new QCheckBox(QString("%1 · CPU package %2 · %3 logical processors").arg(label).arg(v.value("id").toString().toInt()+1).arg(v.value("logical").toInt()));box->setObjectName("pgn-vanity-cpu-"+host+"-"+v.value("id").toString());box->setEnabled(!busy_);choices_->addWidget(box);devices_.append({box,host,mask});}setProperty("pgn_vanity_cpu_packages",devices_.size());}
 void begin(){if(busy_||!load())return;bool selected=gpu_->isChecked();for(const auto& d:devices_)selected|=d.box->isChecked();if(!selected){state_->setText("Select at least one CPU or GPU.");return;}cancelled_=false;resume_=false;finalizing_=false;workers_.clear();winner_={};address_->clear();busy(true);state_->setText("Preparing a protected local key session…");
  local({"--prepare",root_+"/data/runtime/vanity/jobs",prefix_->text()},[this](const auto& o){if(o.value("status")!="prepared"){state_->setText(o.value("error").toString());busy(false);return;}directory_=o.value("directory").toString();job_=o.value("job").toObject();if(cancelled_){finish();return;}if(gpu_->isChecked())checkMining();else launchWorkers();});
 }
 void checkMining(){snapshot([this](const auto& o){if(cancelled_){finish();return;}if(!o.value("owned").isBool()){state_->setText("Mining ownership could not be verified.");busy(false);return;}if(!o.value("owned").toBool()){if(o.value("state")!="OFF"){state_->setText("GPU transition in progress; retry after it settles.");busy(false);return;}launchWorkers();return;}
   const auto m=o.value("manifest").toObject();sourceRun_=m.value("run").toString();const auto pid=QString::number(m.value("pid").toInteger());const auto birth=m.value("birth").toString();const auto hash=m.value("source_profile").toObject().value("image_sha256").toString();
   if(!QRegularExpression("^[0-9a-f]{32}$").match(sourceRun_).hasMatch()||!QRegularExpression("^[0-9TZ:.-]+$").match(birth).hasMatch()||!QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch()){state_->setText("Mining identity is incomplete; no suspension performed.");busy(false);return;}
   if(QMessageBox::question(this,"RTX 3090 is mining","Suspend mining on WIZZ’s RTX 3090 while generating this address? Its checkpoint will be retained and mining will resume automatically afterwards.",QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)!=QMessageBox::Yes){state_->setText("Generation cancelled; mining continues.");busy(false);return;}
   resume_=true;persist("pause_requested");const auto script=QString("& 'C:/PGN/runtime/native-control.ps1' -Action stop-observed -ExpectedRun '%1' -ExpectedPid '%2' -ExpectedBirth '%3' -ExpectedImageSha256 '%4'").arg(sourceRun_,pid,birth,hash);
   control(script,[this](const auto& response){const auto status=response.value("status").toString();if(response.contains("error")){state_->setText(response.value("error").toString());resume_=false;busy(false);return;}state_->setText("Suspending selected GPU · waiting for its exact checkpoint…");polling_=true;waitCount_=0;waitForPause();});
  });}
 void waitForPause(){snapshot([this](const auto& o){if(!resume_)return;if(o.value("owned").isBool()&&!o.value("owned").toBool()&&o.value("state")=="OFF"&&o.value("has_checkpoint").toBool()&&o.value("manifest").toObject().value("run")==sourceRun_){polling_=false;persist("paused_checkpoint_retained");if(cancelled_)finish();else launchWorkers();return;}
  if(++waitCount_>=60){cancelled_=true;state_->setText("Checkpoint transition is taking longer; waiting safely before restoring mining.");}QTimer::singleShot(1000,this,[this]{waitForPause();});});}
 void persist(const QString& state){QSaveFile f(root_+"/data/runtime/vanity/broker.json");QDir().mkpath(root_+"/data/runtime/vanity");if(f.open(QIODevice::WriteOnly)){QJsonArray ids;for(const auto& w:workers_)if(w->host=="remote")ids.append(w->id);f.write(QJsonDocument(QJsonObject{{"schema","pgn-vanity-broker-1"},{"state",state},{"resume_mining",resume_},{"source_run",sourceRun_},{"directory",directory_},{"ui_pid",QString::number(QCoreApplication::applicationPid())},{"remote_workers",ids}}).toJson(QJsonDocument::Compact));f.commit();}}
 void launchWorkers(){if(cancelled_){finish();return;}QStringList localMasks,remoteMasks;for(const auto& d:devices_)if(d.box->isChecked())(d.host=="local"?localMasks:remoteMasks).append(d.mask);
  if(!localMasks.isEmpty())launch("local",localMasks.join(','),0,false);
  if(!remoteMasks.isEmpty())launch("remote",remoteMasks.join(','),quint64(1)<<56,false);
  if(gpu_->isChecked())launch("remote",{},quint64(2)<<56,true);
  persist("searching");state_->setText("Searching with the selected devices · keys stay local.");if(workers_.isEmpty())finish();
 }
 void launch(const QString& host,const QString& masks,quint64 base,bool gpu){
  auto w=std::make_shared<Worker>();w->host=host;w->id=QUuid::createUuid().toString(QUuid::WithoutBraces);auto job=job_;job["id"]=w->id;job["step_base"]=QString::number(base);job["remote_worker"]=host=="remote";job["cancel_path"]=directory_+"/cpu.cancel";auto* p=new QProcess(this);w->process=p;workers_.append(w);
  const auto args=gpu?QStringList{"--worker","C:/PGN/diagnostics/root567-gpu-vanity-20261015-v4/pgn-vanity-gpu.dll","GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac","0"}:QStringList{"--cpu-worker",masks,"0"};
  p->setProgram(host=="local"?helper_:ssh());p->setArguments(host=="local"?args:sshArgs(remote_+" "+args.join(' ')));
  connect(p,&QProcess::started,p,[p,job]{p->write(QJsonDocument(job).toJson(QJsonDocument::Compact)+"\n");p->closeWriteChannel();});
  connect(p,&QProcess::readyReadStandardOutput,this,[this,w]{w->pending+=w->process->readAllStandardOutput();if(w->pending.size()>32768){cancelled_=true;state_->setText("Worker exceeded its response bound; cancelling safely.");stopWorkers();return;}while(w->pending.contains('\n')){const auto n=w->pending.indexOf('\n');const auto o=QJsonDocument::fromJson(w->pending.left(n)).object();w->pending.remove(0,n+1);if(o.value("status")=="match"&&winner_.isEmpty()&&!cancelled_){winner_=o;state_->setText("Match found · stopping the remaining workers before exact verification…");stopWorkers();}else if(o.value("status")=="searching"&&winner_.isEmpty()&&!cancelled_){const auto n=o.value("hashed").toString().toULongLong();const auto ms=qint64(o.value("elapsed_ms").toDouble());if(ms>w->elapsed)w->rate=double(n-w->hashed)*1000./double(ms-w->elapsed);w->hashed=n;w->elapsed=ms;quint64 total{};double rate{};for(const auto& x:workers_){total+=x->hashed;if(!x->finished)rate+=x->rate;}state_->setText(QString("%1 Mkey/s · %2 candidates · %3 s · searching until found or cancelled").arg(rate/1e6,0,'f',2).arg(total).arg(ms/1000.,0,'f',1));setProperty("pgn_vanity_rate_keys_s",rate);setProperty("pgn_vanity_candidates",QString::number(total));}else if(o.value("status")=="error")state_->setText(o.value("error").toString());}});
  auto completed=[this,w]{if(w->finished)return;w->finished=true;w->process->deleteLater();for(const auto& other:workers_)if(!other->finished)return;finish();};
  connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[completed](int,QProcess::ExitStatus){completed();});connect(p,&QProcess::errorOccurred,this,[p,completed](auto){if(p->state()==QProcess::NotRunning)completed();});p->start();
 }
 void stopWorkers(){QFile f(directory_+"/cpu.cancel");if(f.open(QIODevice::WriteOnly))f.write("stop\n");for(const auto& w:workers_)if(w->host=="remote"&&!w->finished&&!cancelling_.contains(w->id)){cancelling_.insert(w->id);remote(remote_+" --cancel-worker "+w->id,[this,id=w->id](const auto&){cancelling_.remove(id);});}}
 void finish(){if(finalizing_)return;for(const auto& w:workers_)if(!w->finished)return;finalizing_=true;checkRemoteFinished(0);}
 void checkRemoteFinished(int index){if(index>=workers_.size()){completeResult();return;}const auto w=workers_[index];if(w->host!="remote"){checkRemoteFinished(index+1);return;}remote(remote_+" --cancel-worker "+w->id,[this,w,index](const auto&){remote(remote_+" --worker-state "+w->id,[this,index](const auto& o){if(o.value("status")=="finished"||o.value("status")=="absent"){checkRemoteFinished(index+1);return;}state_->setText("Waiting for selected worker shutdown before mining resumes…");QTimer::singleShot(1200,this,[this,index]{checkRemoteFinished(index);});});});}
 void completeResult(){if(!winner_.isEmpty()&&!cancelled_){local({"--complete",directory_},[this](const auto& o){if(o.value("status")=="generated"){address_->setText(o.value("address").toString());setProperty("pgn_vanity_generated",true);state_->setText("Address generated · Core ownership verified · private key encrypted locally.");}else state_->setText(o.value("error").toString());restoreMining();},QJsonDocument(winner_).toJson(QJsonDocument::Compact));}else{state_->setText(cancelled_?"Search cancelled. Encrypted session retained.":"Selected workers stopped before a match. Encrypted session retained.");restoreMining();}}
 void restoreMining(){if(!resume_){persist("finished");busy(false);return;}persist("resume_pending");state_->setText(state_->text()+" Restoring mining from its checkpoint…");
  const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);execute(QCoreApplication::applicationFilePath(),{"--start-owned",QString::number(QCoreApplication::applicationPid()),id},{},[this](const auto& o){if(o.contains("error")){state_->setText("Generation finished; mining resume requires attention: "+o.value("error").toString());busy(false);persist("resume_pending");return;}verifyResume();});
 }
 void verifyResume(){snapshot([this](const auto& o){const auto m=o.value("manifest").toObject();if(o.value("owned").toBool()&&(m.value("resume_previous_run")==sourceRun_||m.value("run")==sourceRun_)){resume_=false;persist("finished_mining_resumed");state_->setText(state_->text()+" Mining resumed.");busy(false);return;}QTimer::singleShot(1500,this,[this]{verifyResume();});});}
};
}
QWidget* createVanityView(const QString& root){return new VanityView(root);}
}
