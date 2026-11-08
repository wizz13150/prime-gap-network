#include "update/component_update_service.h"
#include "update/update_paths.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVersionNumber>
#include <QSaveFile>
#include <QUuid>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QDateTime>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pgn::update {
namespace {
constexpr qsizetype MessageLimit=128*1024,ErrorLimit=16*1024;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
}
ComponentUpdateDeployment componentUpdateDeployment(const QString& applicationDirectory,bool testnet){
    const auto root=QFileInfo(applicationDirectory).canonicalFilePath();
    require(!root.isEmpty(),"Installed application directory is unavailable");
    QFile descriptor(root+"/UpdateRuntime/deployment.json");
    require(descriptor.open(QIODevice::ReadOnly)&&descriptor.size()<=8192,"Update deployment is unavailable");
    QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(descriptor.readAll(),&error);
    const auto config=doc.object();
    require(error.error==QJsonParseError::NoError&&config.value("schema")=="PGN_UPDATE_DEPLOYMENT_V1","Update deployment is invalid");
    require(config.value("network").toString()==(testnet?"testnet":"main"),"Update deployment belongs to another network");
    require(QRegularExpression("^[0-9a-f]{64}$").match(config.value("genesis_hash").toString()).hasMatch(),"Update genesis is invalid");
    require(config.value("helper")=="UpdateRuntime/horizon-updater.exe","Unexpected update helper path");
    const auto helper=QFileInfo(root+"/"+config.value("helper").toString()).canonicalFilePath();
    require(helper.startsWith(root+"/",Qt::CaseInsensitive),"Update helper is outside its deployment");
    QFile file(helper);
    require(file.open(QIODevice::ReadOnly)&&file.size()>0&&file.size()<=64*1024*1024,"Update helper is unavailable");
    QCryptographicHash digest(QCryptographicHash::Sha256);
    require(digest.addData(&file)&&QString::fromLatin1(digest.result().toHex())==config.value("sha256").toString(),"Update helper fingerprint differs");
    return {helper,config};
}

std::optional<int> componentVersionComparison(QString installed,QString available){
    const QRegularExpression numeric("^(?:0|[1-9][0-9]{0,8})(?:[.-](?:0|[1-9][0-9]{0,8})){0,7}$");
    if(!numeric.match(installed).hasMatch()||!numeric.match(available).hasMatch())return {};
    installed.replace('-','.');available.replace('-','.');
    return QVersionNumber::compare(QVersionNumber::fromString(installed).normalized(),QVersionNumber::fromString(available).normalized());
}
QString componentUpdateFailureMessage(const QString& detail){
 const auto value=detail.left(512);
 const auto http=QRegularExpression("\\b(?:status|HTTP)\\D{0,32}(5[0-9]{2}|429)\\b",QRegularExpression::CaseInsensitiveOption).match(value);
 if(http.hasMatch())return "GitHub update service is temporarily unavailable (HTTP "+http.captured(1)+"). Version status could not be checked. Try again later.";
 if(QRegularExpression("getaddrinfo|name.?resolution|connection.*(?:failed|refused|reset)|timed? ?out|network.*unreachable|WinError (?:10060|10061|11001)",QRegularExpression::CaseInsensitiveOption).match(value).hasMatch())
  return "Cannot reach the update server. Check your Internet connection and try again. Version status is unconfirmed. Details: "+value;
 if(value=="Update deployment is unavailable")return "Update setup is incomplete in this application folder. Download and extract the corrected complete Horizon package.";
 return value;
}
bool validComponentUpdateProgress(const QJsonObject& value){
 const auto done=value.value("completed"),total=value.value("total");
 return value.size()==5&&value.value("schema")=="PGN_UPDATE_PROGRESS_V1"
  &&QStringList{"metadata","download","verify","prepare","drain","startup","committed"}.contains(value.value("stage").toString())
  &&QStringList{"","ui","core","gpu","cpu"}.contains(value.value("component").toString())
  &&done.isDouble()&&total.isDouble()&&done.toDouble()==done.toInteger(-1)&&total.toDouble()==total.toInteger(-1)
  &&done.toInteger(-1)>=0&&total.toInteger(-1)>=done.toInteger()&&total.toDouble()<9007199254740992.;
}
QJsonObject componentUpdateRequest(const ComponentUpdateDeployment& deployment,const QJsonObject& snapshot,
    const QString& applicationDirectory,const QString& mutableDataRoot,const QString& action,const QStringList& selected){
    const auto config=deployment.context;
    QJsonArray installed,selection;
    // Catalogue discovery is read-only and must also describe an older public
    // build. Preparation/installation retain the actual verified version floor.
    for(const auto& value:action=="check"?QJsonArray{}:snapshot.value("components").toArray()){
        const auto item=value.toObject();if(item.value("state")!="matched_local_manifest")continue;
        installed.append(QJsonObject{{"id",item.value("id")},{"version",item.value("version")},{"abi",config.value("abi")}});
    }
    for(const auto& id:selected)selection.append(id);
    const auto state=updateStateDirectory(config.value("genesis_hash").toString());
    require(!state.isEmpty(),"Update storage is unavailable for this network");
    QJsonObject context{{"schema","PGN_COMPONENT_STAGE_CONTEXT_V1"},{"network",config.value("network")},
        {"genesis_hash",config.value("genesis_hash")},{"platform",QSysInfo::kernelType()=="winnt"?"windows-x86_64":"linux-x86_64"},
        {"compatibility_set",config.value("compatibility_set")},{"minimum_sequence",1},
        {"installed",installed},{"protected_roots",QJsonArray{applicationDirectory,mutableDataRoot}}};
    QJsonObject request{{"schema","PGN_UPDATE_REQUEST_V1"},{"action",action},{"context",context},{"selected",selection},{"cache",state+"/cache"}};
    if(action=="prepare_install"||action=="install"){request.insert("base_cohort",applicationDirectory);request.insert("installation_store",state+"/installation");}
    if(action=="recover"){request.remove("selected");request.insert("installation_store",state+"/installation");}
    return request;
}

bool componentUpdateNeedsRecovery(const QString& genesis){
    const auto state=updateStateDirectory(genesis);if(state.isEmpty())return false;
    QFile file(state+"/installation/transaction.json");
    if(!file.exists())return false;
    if(!file.open(QIODevice::ReadOnly)||file.size()>256*1024)return true;
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);const auto value=doc.object();
    if(error.error!=QJsonParseError::NoError||value.value("schema")!="PGN_COHORT_ACTIVATION_V1")return true;
    const auto status=value.value("state").toString();
    return status!="committed"&&!(value.value("restoration_completed").toBool()&&
        QStringList{"shutdown_failed","shutdown_refused","startup_failed","activation_failed","recovered_previous"}.contains(status));
}

QJsonObject currentUpdateActivation(const QString& applicationRoot,const QString& mutableDataRoot){
#ifdef Q_OS_WIN
    FILETIME created{},exited{},kernel{},user{};
    require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user),"Current process identity is unavailable");
    const quint64 birth=(quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    return {{"schema","PGN_WINDOWS_UPDATE_ACTIVATION_V1"},{"ui_pid",QCoreApplication::applicationPid()},
        {"ui_birth",QString::number(birth)},{"application_root",QDir::cleanPath(applicationRoot)},
        {"data_root",QDir::cleanPath(mutableDataRoot)}};
#else
    Q_UNUSED(applicationRoot);Q_UNUSED(mutableDataRoot);
    throw std::runtime_error("Integrated installation is currently qualified for Windows.");
#endif
}

QString startDetachedComponentInstall(const ComponentUpdateDeployment& deployment,const QJsonObject& request){
    require(request.value("schema")=="PGN_UPDATE_REQUEST_V1"&&(request.value("action")=="install"||request.value("action")=="recover"),"Explicit installation or recovery request required");
    require(deployment.context.value("owned_update_protocol").toInt()==1,"Installed client has no ordered update protocol");
    const auto encoded=QJsonDocument(request).toJson(QJsonDocument::Compact);
    require(encoded.size()<=MessageLimit,"Installation request exceeds its bound");
    const auto cache=request.value("cache").toString();require(QFileInfo(cache).isAbsolute(),"Absolute update cache required");
    const auto session=cache+"/sessions/"+QUuid::createUuid().toString(QUuid::Id128);
    require(QDir().mkpath(session),"Cannot create the owned update session");
    QSaveFile file(session+"/request.json");require(file.open(QIODevice::WriteOnly)&&file.write(encoded)==encoded.size()&&file.commit(),"Cannot persist the installation request");
    QProcess helper;helper.setProgram(deployment.helper);helper.setArguments({"--request-file",session+"/request.json","--result-file",session+"/result.json"});
    helper.setWorkingDirectory(QFileInfo(deployment.helper).absolutePath());
    helper.setStandardInputFile(QProcess::nullDevice());helper.setStandardOutputFile(QProcess::nullDevice());helper.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    helper.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS;});
#endif
    require(helper.startDetached(),"The installation helper could not start; installed components are unchanged");
    return session+"/result.json";
}

ComponentUpdateService::ComponentUpdateService(ComponentUpdateDeployment deployment,QObject* parent)
    :QObject(parent),deployment_(std::move(deployment)),process_(new QProcess(this)),timer_(new QTimer(this)){
    timer_->setSingleShot(true);
#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS;});
#endif
    connect(process_,&QProcess::readyReadStandardOutput,this,[this]{
        output_+=process_->readAllStandardOutput();
        if(output_.size()>MessageLimit){oversized_=true;process_->kill();}
    });
    connect(process_,&QProcess::readyReadStandardError,this,[this]{
        const auto chunk=process_->readAllStandardError();errors_+=chunk;if(errors_.size()>ErrorLimit)errors_=errors_.right(ErrorLimit);
        progressBuffer_+=chunk;if(progressBuffer_.size()>ErrorLimit){progressBuffer_.clear();return;}
        while(progressBuffer_.contains('\n')){const auto end=progressBuffer_.indexOf('\n');const auto line=progressBuffer_.left(end);progressBuffer_.remove(0,end+1);if(line.size()>4096)continue;
            const auto value=QJsonDocument::fromJson(line).object();if(validComponentUpdateProgress(value)&&onProgress){const QPointer<ComponentUpdateService> alive(this);onProgress(value);if(!alive)return;}
        }
    });
    connect(process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){complete();});
    connect(process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)complete();});
    connect(timer_,&QTimer::timeout,this,[this]{timedOut_=true;process_->kill();});
}
ComponentUpdateService::~ComponentUpdateService(){
    settled_=true;timer_->stop();
    if(process_->state()!=QProcess::NotRunning){process_->kill();process_->waitForFinished(1000);}
}
bool ComponentUpdateService::busy()const{return !settled_;}
bool ComponentUpdateService::start(const QJsonObject& request){
    if(busy())return false;
    const auto action=request.value("action").toString();
    if(action!="check"&&action!="prepare"&&action!="prepare_install")return false;
    const auto data=QJsonDocument(request).toJson(QJsonDocument::Compact);
    if(data.size()>MessageLimit)return false;
    output_.clear();errors_.clear();progressBuffer_.clear();settled_=false;cancelled_=oversized_=timedOut_=false;action_=action;
    process_->setProgram(deployment_.helper);process_->setArguments({});process_->setWorkingDirectory(QFileInfo(deployment_.helper).absolutePath());
    disconnect(startedConnection_);
    startedConnection_=connect(process_,&QProcess::started,this,[this,data]{disconnect(startedConnection_);process_->write(data);process_->closeWriteChannel();});
    const QPointer<ComponentUpdateService> guard(this);
    if(onStatus)onStatus(action=="check"?"Checking signed component versions…":"Downloading and verifying the selected components…");
    if(!guard)return true;
    if(cancelled_){complete();return true;}
    timer_->start(action=="check"?60000:600000);process_->start();return true;
}
void ComponentUpdateService::cancel(){if(busy()){cancelled_=true;process_->kill();}}
void ComponentUpdateService::complete(){
    if(settled_)return;settled_=true;timer_->stop();disconnect(startedConnection_);
    output_+=process_->readAllStandardOutput();
    QString error;
    if(cancelled_)error="Update preparation cancelled. Installed components are unchanged.";
    else if(timedOut_)error="The update operation timed out. Installed components are unchanged.";
    else if(oversized_||output_.size()>MessageLimit)error="The update helper response exceeded its bound.";
    QJsonParseError parseError;const auto document=QJsonDocument::fromJson(output_,&parseError);const auto result=document.object();
    if(error.isEmpty()&&(parseError.error!=QJsonParseError::NoError||!document.isObject()))error="The update helper returned no valid result.";
    if(error.isEmpty()&&(process_->exitStatus()!=QProcess::NormalExit||process_->exitCode()!=0))error=result.value("error").toString("The update operation failed.").left(512);
    if(error.isEmpty()&&!result.value("publisher_authenticated").toBool())error="The update result has no verified publisher identity.";
    const auto expected=action_=="check"?QString("PGN_AUTHENTICATED_COMPONENT_VIEW_V1"):action_=="prepare"?QString("PGN_AUTHENTICATED_STAGE_RECEIPT_V1"):QString("PGN_UPDATE_INSTALL_PREPARATION_V1");
    if(error.isEmpty()&&result.value("schema").toString()!=expected)error="The update helper returned an unexpected result type.";
    if(!error.isEmpty()){if(onError)onError(componentUpdateFailureMessage(error));return;}
    auto observed=result;if(action_=="check")observed.insert("checked_at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if(onResult)onResult(observed);
}
}
