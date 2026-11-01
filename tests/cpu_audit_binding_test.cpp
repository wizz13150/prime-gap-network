#include "mining/cpu_fleet.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QSysInfo>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QCoreApplication::setOrganizationName("PGN-CPU-TEST");QCoreApplication::setApplicationName("isolated-audit-binding");QTemporaryDir workspace;if(!workspace.isValid())return 3;
 const auto host=QSysInfo::machineHostName(),id="CPU/"+host.toUpper()+"/0";pgn::mining::CpuFleet fleet;int stage=0;bool failed=false;QJsonObject latest;
 pgn::mining::CpuAuditWork work;work.requestId=QString(64,'b');work.slot=0;work.attempt=0;
 QJsonArray parts;const boost::multiprecision::cpp_int start("150000000000000000000");for(int i:{0,9,20,40,99})parts.append(QJsonObject{{"index",i},{"start",QString::fromStdString((start+boost::multiprecision::cpp_int("2000000000000")*i).convert_to<std::string>())},{"end",QString::fromStdString((start+boost::multiprecision::cpp_int("2000000000000")*(i+1)).convert_to<std::string>())}});
 work.serviceRequest={{"schema","PGN_EXPLICIT_CPU_AUDIT_REQUEST_V1"},{"ticket_id",QString(64,'a')},{"ticket_start","150000000000000000000"},{"ticket_end","150000200000000000000"},{"partitions",parts}};work.requestSha256=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(work.serviceRequest).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
 auto fail=[&](const QString& error){std::cerr<<error.toStdString()<<'\n';failed=true;fleet.requestClose();QTimer::singleShot(1000,&app,&QCoreApplication::quit);};
 fleet.onTelemetry=[&](const QString&,QJsonObject inventory,QJsonObject status,bool connected){
  if(!connected)return;latest=status;if(!status.value("worker_pids").toArray().isEmpty()||status.value("threads_active").toInt()>0){fail("Paused audit binding started unrequested computation");return;}
  if(stage==0&&!inventory.isEmpty()&&status.value("state")=="IDLE"){
   stage=1;if(!fleet.attachAudit(id,work)||!fleet.attachAudit(id,work)){fail("Idempotent audit attachment refused");return;}
   auto other=work;other.attempt=1;if(fleet.attachAudit(id,other)){fail("Existing assignment silently retagged");return;}
  }
 };
 fleet.onAuditAttached=[&](const QString& device,pgn::mining::CpuAuditWork attached){if(stage!=1||device!=id||attached.requestSha256!=work.requestSha256||latest.value("request_sha256")!=work.requestSha256){fail("Native audit request digest differs from its C++ assignment");return;}stage=2;fleet.requestClose();};
 fleet.onAuditComputation=[&](auto,auto,auto){fail("A paused partial audit was returned as complete");};
 fleet.onAuditError=[&](auto,auto error){fail(error);};
 fleet.onCloseReady=[&]{if(stage==2&&fleet.allStopped())app.quit();else fail("Audit service close was not confirmed");};
 fleet.add({QString::fromLocal8Bit(argv[1]),{"--workspace",workspace.path(),"--piece-positions","100000000"},id,host});QTimer::singleShot(12000,&app,[&]{fail("Audit binding test timeout");});app.exec();
 if(failed||stage!=2||!fleet.allStopped())return 1;std::cout<<"PASS actual native CPU service: exact request digest, idempotent attachment, no retag/overwrite, paused zero-worker binding and clean close; no audit computation or network acceptance claimed\n";
}
