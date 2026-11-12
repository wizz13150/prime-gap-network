#include "ui/diagnostic_capture_view.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSaveFile>
#include <QProcess>
#include <QTimer>
#include <QJsonDocument>
#include <QDateTime>
#include <QFrame>
#include <QPointer>
#include <cmath>
#include <memory>

namespace pgn::ui {
namespace {
constexpr qsizetype CaptureBudget=2*1024*1024;
constexpr int SampleLimit=1800;
class DiagnosticCaptureView final:public QWidget {
 QString exporter_;std::function<QJsonObject()> sample_;QTimer timer_;QJsonArray samples_;qsizetype bytes_{};bool exporting_{};
 QLabel* status_{};QPushButton *start_{},*stop_{},*save_{};
 QLabel* storageStatus_{};QPushButton *inspectStorage_{},*cleanStorage_{};DiagnosticStorageRequest storageRequest_;bool storageBusy_{};quint64 storageGeneration_{};
 void storage(const QString& action){
  if(!storageRequest_||storageBusy_)return;
  storageBusy_=true;const auto generation=++storageGeneration_;inspectStorage_->setEnabled(false);cleanStorage_->setEnabled(false);
  storageStatus_->setText(action=="clean_storage"?"Cleaning verified unused files…":"Inspecting local update storage…");
  const QPointer<DiagnosticCaptureView> guard(this);
  storageRequest_(action,[guard,generation](const QJsonObject& result,const QString& error){
   if(!guard||generation!=guard->storageGeneration_)return;
   guard->storageBusy_=false;guard->inspectStorage_->setEnabled(true);
   const auto cache=result.value("cache").toObject();
   const auto number=[](const QJsonValue& v){return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=0;};
   const bool valid=result.value("schema")=="PGN_DIAGNOSTIC_STORAGE_V1"&&number(cache.value("total_bytes"))&&number(cache.value("reclaimable_bytes"))&&!cache.value("path").toString().isEmpty();
   if(!error.isEmpty()||!valid){guard->storageStatus_->setText(error.isEmpty()?"Storage inspection returned no valid result.":error);return;}
   guard->cleanStorage_->setEnabled(true);
   const auto mib=[](double bytes){return QString::number(bytes/1048576.0,'f',1)+" MiB";};
   const bool cleaned=result.value("cleaned").toBool();
   QString text=cleaned?"Update cache reclaimed: "+mib(result.value("reclaimed_bytes").toDouble()):"Update cache: "+mib(cache.value("total_bytes").toDouble())+" · reclaimable: "+mib(cache.value("reclaimable_bytes").toDouble());
   text+="\nCache: "+cache.value("path").toString()+"\nProgram copies: "+result.value("installation_path").toString();
   const auto removal=result.value("installation_cleanup").toObject();if(cleaned&&removal.contains("removed"))text+="\nUnused installations removed: "+QString::number(removal.value("removed").toInt());
   const auto warning=result.value("warning").toString();if(!warning.isEmpty())text+="\n"+warning;
   guard->storageStatus_->setText(text);guard->setProperty("pgn_storage_maintenance",result);
  });
 }
 void state(const QString& message){status_->setText(message);setProperty("pgn_capture_status",message);setProperty("pgn_capture_samples",samples_.size());setProperty("pgn_capture_bytes",bytes_);}
 void capture(){
  auto value=sample_?sample_():QJsonObject{};value.insert("observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  const auto encoded=QJsonDocument(value).toJson(QJsonDocument::Compact);
  if(encoded.size()>65536||samples_.size()>=SampleLimit||bytes_+encoded.size()+1024>CaptureBudget){stop();state("Capture capacity reached · save the retained recording");return;}
  samples_.append(value);bytes_+=encoded.size()+1;state(QString("Recording · %1 samples · %2 KiB / 2048 KiB").arg(samples_.size()).arg((bytes_+1023)/1024));
 }
 void stop(){timer_.stop();start_->setEnabled(!exporting_);stop_->setEnabled(false);save_->setEnabled(!exporting_&&!samples_.isEmpty());state(QString("Stopped · %1 samples retained in memory").arg(samples_.size()));}
public:
 DiagnosticCaptureView(QString exporter,std::function<QJsonObject()> sample,QWidget* parent):QWidget(parent),exporter_(std::move(exporter)),sample_(std::move(sample)),timer_(this){
  setObjectName("pgn-test-diagnostics");auto* box=new QVBoxLayout(this);box->setContentsMargins(24,20,24,20);box->setSpacing(16);
  auto* title=new QLabel("Testnet diagnostics",this);title->setStyleSheet("font-size:22px;font-weight:600;color:#e7f9f1;");box->addWidget(title);
  auto* description=new QLabel("Record application, network and device status, then save a ZIP for troubleshooting.\nThe capture stays in memory until you save it. Wallet files, keys, credentials, audit answers and gap tables are excluded.",this);description->setWordWrap(true);description->setStyleSheet("font-size:13px;color:#a8c7cc;");box->addWidget(description);
  auto* actions=new QHBoxLayout;start_=new QPushButton("Start recording",this);stop_=new QPushButton("Stop",this);save_=new QPushButton("Save ZIP",this);
  start_->setObjectName("pgn-diagnostics-start");stop_->setObjectName("pgn-diagnostics-stop");save_->setObjectName("pgn-diagnostics-save");
  for(auto* button:{start_,stop_,save_}){button->setMinimumHeight(36);button->setStyleSheet("font-size:12px;padding:6px 16px;");}
  start_->setStyleSheet("font-size:12px;font-weight:600;padding:6px 16px;background:#e8bf74;color:#091820;border:1px solid #e8bf74;border-radius:7px;");
  start_->setToolTip("Start a new recording; replaces the current unsaved capture");stop_->setEnabled(false);save_->setEnabled(false);
  actions->addWidget(start_);actions->addWidget(stop_);actions->addWidget(save_);actions->addStretch();box->addLayout(actions);
  status_=new QLabel(this);status_->setObjectName("pgn-diagnostics-status");status_->setWordWrap(true);status_->setStyleSheet("font-size:13px;color:#91dfbb;padding:14px;background:#071e26;border:1px solid #28515b;border-radius:8px;");box->addWidget(status_);state("No recording · no diagnostic file has been created");
  auto* maintenance=new QFrame(this);maintenance->setObjectName("pgn-diagnostics-storage");auto* storageBox=new QVBoxLayout(maintenance);storageBox->setContentsMargins(14,12,14,12);storageBox->setSpacing(10);
  auto* storageTitle=new QLabel("Storage cleanup",maintenance);storageTitle->setStyleSheet("font-size:15px;font-weight:600;");storageBox->addWidget(storageTitle);
  auto* storageDescription=new QLabel("Inspect and remove downloaded update files and unused program copies. Wallets, mining results and trusted update history are preserved.",maintenance);storageDescription->setWordWrap(true);storageBox->addWidget(storageDescription);
  auto* storageButtons=new QHBoxLayout;inspectStorage_=new QPushButton("Analyze storage",maintenance);cleanStorage_=new QPushButton("Clean unused files",maintenance);
  inspectStorage_->setObjectName("pgn-diagnostics-storage-inspect");cleanStorage_->setObjectName("pgn-diagnostics-storage-clean");
  for(auto* button:{inspectStorage_,cleanStorage_}){button->setMinimumHeight(34);button->setEnabled(false);storageButtons->addWidget(button);}storageButtons->addStretch();storageBox->addLayout(storageButtons);
  storageStatus_=new QLabel("Storage maintenance is not configured.",maintenance);storageStatus_->setObjectName("pgn-diagnostics-storage-status");storageStatus_->setTextFormat(Qt::PlainText);storageStatus_->setWordWrap(true);storageStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);storageBox->addWidget(storageStatus_);
  maintenance->setStyleSheet("QFrame#pgn-diagnostics-storage{background:#071e26;border:1px solid #28515b;border-radius:8px;}");box->addWidget(maintenance);box->addStretch();
  connect(inspectStorage_,&QPushButton::clicked,this,[this]{storage("inspect_storage");});connect(cleanStorage_,&QPushButton::clicked,this,[this]{storage("clean_storage");});
  timer_.setInterval(1000);connect(&timer_,&QTimer::timeout,this,[this]{capture();});
  connect(start_,&QPushButton::clicked,this,[this]{if(exporting_)return;samples_={};bytes_=0;setProperty("pgn_capture_saved",QVariant{});start_->setEnabled(false);stop_->setEnabled(true);save_->setEnabled(false);timer_.start();capture();});
  connect(stop_,&QPushButton::clicked,this,[this]{stop();});
  connect(save_,&QPushButton::clicked,this,[this]{
   stop();const auto file=QFileDialog::getSaveFileName(this,"Save diagnostic capture",QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/PGN-Horizon-diagnostics-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+".zip","ZIP archive (*.zip)");
   if(!file.isEmpty())exportTo(file);
  });
 }
 void setStorageActions(DiagnosticStorageRequest request){storageRequest_=std::move(request);inspectStorage_->setEnabled(bool(storageRequest_));cleanStorage_->setEnabled(false);storageStatus_->setText(storageRequest_?"Analyze storage to see its locations and reclaimable space.":"Storage maintenance is unavailable.");}
 void exportTo(const QString& file){
  if(exporting_||samples_.isEmpty()||file.isEmpty())return;stop();
  const auto input=QJsonDocument(QJsonObject{{"schema","PGN_DIAGNOSTIC_CAPTURE_V1"},{"samples",samples_}}).toJson(QJsonDocument::Compact);
  if(input.size()>CaptureBudget){state("Capture exceeds the export budget; recording retained");return;}
  exporting_=true;setProperty("pgn_capture_saved",QVariant{});start_->setEnabled(false);save_->setEnabled(false);state("Saving diagnostic ZIP…");
  auto* process=new QProcess(this);auto* deadline=new QTimer(process);deadline->setSingleShot(true);
  auto output=std::make_shared<QByteArray>(),errors=std::make_shared<QByteArray>();auto done=std::make_shared<bool>(false);
  const auto finish=[this,process,deadline,output,done,file](bool success,const QString& error){
   if(*done)return;*done=true;deadline->stop();if(process->state()!=QProcess::NotRunning)process->kill();
   bool saved=false;
   if(success&&output->startsWith("PK")&&output->size()<=CaptureBudget){QSaveFile target(file);saved=target.open(QIODevice::WriteOnly)&&target.write(*output)==output->size()&&target.commit();}
   exporting_=false;start_->setEnabled(true);save_->setEnabled(!samples_.isEmpty());setProperty("pgn_capture_saved",saved);
   state(saved?"Saved · "+file:error.isEmpty()?QString("ZIP could not be saved; recording retained"):error+" · recording retained");process->deleteLater();
  };
  connect(process,&QProcess::readyReadStandardOutput,this,[process,output,finish]{output->append(process->readAllStandardOutput());if(output->size()>CaptureBudget)finish(false,"Diagnostic ZIP exceeds its budget");});
  connect(process,&QProcess::readyReadStandardError,this,[process,errors]{errors->append(process->readAllStandardError());if(errors->size()>2048)errors->remove(0,errors->size()-2048);});
  connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false,"Diagnostic exporter could not start");});
  connect(deadline,&QTimer::timeout,this,[finish]{finish(false,"Diagnostic export timed out");});
  connect(process,&QProcess::started,this,[process,input]{process->write(input);process->closeWriteChannel();});
  connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[process,output,finish](int code,QProcess::ExitStatus status){output->append(process->readAllStandardOutput());finish(code==0&&status==QProcess::NormalExit,code?QString("Diagnostic export was refused"):QString{});});
  process->setProgram(exporter_);process->setArguments({"--export-diagnostics"});deadline->start(20000);process->start();
 }
};
}
QWidget* createDiagnosticCaptureView(QString exporter,std::function<QJsonObject()> sample,QWidget* parent){return new DiagnosticCaptureView(std::move(exporter),std::move(sample),parent);}
void setDiagnosticStorageActions(QWidget* view,DiagnosticStorageRequest request){if(auto* capture=dynamic_cast<DiagnosticCaptureView*>(view))capture->setStorageActions(std::move(request));}
void saveDiagnosticCapture(QWidget* view,const QString& filename){if(auto* capture=dynamic_cast<DiagnosticCaptureView*>(view))capture->exportTo(filename);}
}
