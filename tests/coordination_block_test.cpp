#include "wallet/coordination_block.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);using namespace pgn::wallet;
 if(argc==4){QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::ReadOnly)||file.size()>262144)return 2;const auto work=QJsonDocument::fromJson(file.readAll()).object();const auto result=coordinationBlock(work,QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]));if(!result.candidate){std::cerr<<result.error.toStdString();return 3;}std::cout<<QJsonDocument(QJsonObject{{"block",result.candidate->block},{"hash",result.candidate->hash},{"attempts",qint64(result.candidate->attempts)},{"accepted",false}}).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;}
 int checks=0;auto require=[&](bool okay){++checks;if(!okay)throw std::runtime_error("coordination check "+std::to_string(checks));};
 const QString genesis(64,'a'),parent=QString("0123456789abcdef").repeated(4);QByteArray header(112,'\0');auto previous=QByteArray::fromHex(parent.toLatin1());std::reverse(previous.begin(),previous.end());header.replace(4,32,previous);
 QJsonObject work{{"schema","PGN_NATIVE112_BOOTSTRAP_TEMPLATE1"},{"genesis",genesis},{"parent",parent},{"work_verified",false},{"nonce_offset",82},{"nonce_bytes",8},{"header",QString::fromLatin1(header.toHex())},{"block",QString::fromLatin1((header+QByteArray("fixture body")).toHex())},{"numeric_target",QString(64,'f')}};
 const auto result=coordinationBlock(work,genesis,parent,7,1);require(result.candidate.has_value());require(result.candidate->attempts==1);auto bytes=QByteArray::fromHex(result.candidate->block.toLatin1());require(bytes[82]==7&&bytes.mid(112)=="fixture body");
 auto digest=QCryptographicHash::hash(QCryptographicHash::hash(bytes.first(112),QCryptographicHash::Sha256),QCryptographicHash::Sha256);std::reverse(digest.begin(),digest.end());require(result.candidate->hash==QString::fromLatin1(digest.toHex()));
 std::atomic_bool stop{true};require(!coordinationBlock(work,genesis,parent,0,1,&stop).candidate);
 for(const auto& change:{QJsonObject{{"schema","other"}},QJsonObject{{"parent",QString(64,'0')}},QJsonObject{{"genesis",QString(64,'0')}},QJsonObject{{"work_verified",true}},QJsonObject{{"nonce_offset",81}},QJsonObject{{"nonce_bytes",9}},QJsonObject{{"numeric_target",QString(64,'0')}},QJsonObject{{"block","BAD"}}}){auto invalid=work;for(auto i=change.begin();i!=change.end();++i)invalid.insert(i.key(),i.value());require(!coordinationBlock(invalid,genesis,parent).candidate);}
 require(!coordinationBlock(work,genesis,parent,0,0).candidate);require(!coordinationBlock(work,genesis,parent,0,1000001).candidate);
 std::cout<<"PASS "<<checks<<" bounded native coordination construction checks; no acceptance or reward claim\n";
}
