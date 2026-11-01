#include "verification/archive_verifier.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);const auto arguments=QCoreApplication::arguments();
 const bool drain=arguments.size()==7&&arguments[6]=="--drain";
 if(arguments.size()<3||arguments.size()>7||(arguments.size()==7&&!drain))return 1;
 pgn::verification::ArchiveVerificationPolicy policy;
 bool valid=true,number=true;
 if(arguments.size()>3){policy.after=arguments[3].toLongLong(&number);valid&=number;}
 if(arguments.size()>4){policy.through=arguments[4].toLongLong(&number);valid&=number;}
 if(arguments.size()>5){policy.limit=arguments[5].toInt(&number);valid&=number;}
 if(!valid||(drain&&policy.through<=0))return 2; // A finite job never follows an advancing source head.
 QFile image(QCoreApplication::applicationFilePath());QCryptographicHash hash(QCryptographicHash::Sha256);
 if(!image.open(QIODevice::ReadOnly)||!hash.addData(&image))return 3;
 const auto validator=QString::fromLatin1(hash.result().toHex());
 qint64 processed=0,evaluated=0,cached=0,verified=0,rejected=0;
 while(true){
 const auto previous=policy.after;
 pgn::verification::ArchiveVerificationBatch result;
 const QStorageInfo volume(QFileInfo(arguments[2]).absolutePath());
 if(drain&&(!volume.isValid()||!volume.isReady()||volume.bytesAvailable()<2LL*1024*1024*1024)){
  result.after=policy.after;result.through=policy.through;
  result.error="Disk reserve below2GiB; committed prefix retained, no further verification writes";
 }else result=pgn::verification::verifyArchivePage(arguments[1],arguments[2],validator,policy);
 const QJsonObject summary{{"schema","pgn-finite-archive-verification-1"},{"validator_sha256",validator},
  {"after",QString::number(result.after)},{"through",QString::number(result.through)},
  {"processed",result.processed},{"evaluated",result.evaluated},{"cached",result.cached},
  {"verified",result.verified},{"rejected",result.rejected},{"more",result.more},
  {"cancelled",result.cancelled},{"error",result.error},{"network_credit",false},{"range_coverage_claim",false},{"cpu_mining",false}};
 std::cout<<QJsonDocument(summary).toJson(QJsonDocument::Compact).constData()<<std::endl;
 processed+=result.processed;evaluated+=result.evaluated;cached+=result.cached;verified+=result.verified;rejected+=result.rejected;
 const bool okay=result.error.isEmpty()&&!result.cancelled;
 if(!drain)return okay?0:4;
 if(!okay||!result.more){
  const QJsonObject final{{"schema","pgn-finite-archive-verification-job-1"},{"validator_sha256",validator},
   {"after",QString::number(result.after)},{"through",QString::number(result.through)},
   {"processed",QString::number(processed)},{"evaluated",QString::number(evaluated)},
   {"cached",QString::number(cached)},{"verified",QString::number(verified)},{"rejected",QString::number(rejected)},
   {"job_complete",okay&&!result.more},{"error",result.error},{"network_credit",false},
   {"range_coverage_claim",false},{"cpu_mining",false}};
  std::cout<<QJsonDocument(final).toJson(QJsonDocument::Compact).constData()<<std::endl;return okay?0:4;
 }
 if(result.after<=previous||result.through!=policy.through)return 5;
 policy.after=result.after;
 }
}
