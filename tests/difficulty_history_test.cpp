#include "wallet/difficulty_history.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QJsonValue>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <sqlite3.h>
#include <iostream>
#define REQUIRE(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
static QString hash(qint64 n){return QString::fromLatin1(QCryptographicHash::hash(QByteArray::number(n),QCryptographicHash::Sha256).toHex());}
static QJsonObject header(qint64 n,const QJsonValue& difficulty){QJsonObject result{{"hash",hash(n)},{"height",n},{"difficulty",difficulty},{"extension","literal $(never-execute)"}};if(n)result.insert("previousblockhash",hash(n-1));return result;}
static qint64 count(const QString& file,const char* table){sqlite3* db{};if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)return -1;const auto sql=QByteArray("SELECT count(*) FROM ")+table;sqlite3_stmt* statement{};sqlite3_prepare_v2(db,sql.constData(),-1,&statement,nullptr);qint64 value=-1;if(sqlite3_step(statement)==SQLITE_ROW)value=sqlite3_column_int64(statement,0);sqlite3_finalize(statement);sqlite3_close(db);return value;}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir temp;REQUIRE(temp.isValid());using namespace pgn::wallet;
 const QString file=temp.path()+"/history.sqlite",genesis=hash(0);DifficultyHistoryStore store(file);
 const CoreEpoch owner{42,"original-birth",1};const auto utc=QDateTime::fromString("2026-10-13T12:00:00.123Z",Qt::ISODateWithMs);
 REQUIRE(!store.summary(genesis).available);REQUIRE(!store.find(genesis,genesis));REQUIRE(!QFileInfo::exists(file));
 REQUIRE(!store.record({},genesis,genesis,header(0,1.),utc).isEmpty());REQUIRE(!QFileInfo::exists(file));
 // Reverse backfill must not advertise completeness until genesis is reached.
 QElapsedTimer phase;phase.start();constexpr int length=5002;for(int i=length-1;i>0;--i)REQUIRE(store.record(owner,genesis,hash(i),header(i,i==7?QJsonValue():QJsonValue(double(i))),utc).isEmpty());
 std::cout<<"pending_header_ms="<<phase.elapsed()<<std::endl;phase.restart();
 ChainObservation anchor{owner,genesis,length-1,utc,false,hash(length-1)};
 REQUIRE(!store.activate(anchor,anchor.tipHash).isEmpty());REQUIRE(!store.summary(genesis).available);
 REQUIRE(store.record(owner,genesis,genesis,header(0,0.),utc).isEmpty());REQUIRE(store.activate(anchor,anchor.tipHash).isEmpty());
 std::cout<<"genesis_link_publish_ms="<<phase.elapsed()<<std::endl;
 auto summary=store.summary(genesis);REQUIRE(summary.available);REQUIRE(summary.height==length-1);REQUIRE(summary.points==length-1&&summary.missing==1);REQUIRE(summary.minimum==0.&&summary.maximum==double(length-1));REQUIRE(summary.observedUtc==utc);
 REQUIRE(count(file,"difficulty_headers")==length);REQUIRE(count(file,"difficulty_observations")==length);
 REQUIRE(store.record(owner,genesis,genesis,header(0,0.),utc).isEmpty());REQUIRE(count(file,"difficulty_observations")==length);
 auto conflict=header(0,2.);REQUIRE(!store.record(owner,genesis,genesis,conflict,utc.addMSecs(1)).isEmpty());REQUIRE(count(file,"difficulty_observations")==length);
 auto wrongParent=header(length,1.);wrongParent.insert("previousblockhash",genesis);REQUIRE(!store.record(owner,genesis,hash(length),wrongParent,utc).isEmpty());
 auto fork=header(8,42.);fork.insert("hash",hash(-8));REQUIRE(store.record(owner,genesis,hash(-8),fork,utc.addMSecs(1)).isEmpty());
 auto forkAnchor=anchor;forkAnchor.height=8;forkAnchor.tipHash=hash(-8);forkAnchor.observedUtc=utc.addMSecs(1);REQUIRE(store.activate(forkAnchor,forkAnchor.tipHash).isEmpty());
 summary=store.summary(genesis);REQUIRE(summary.height==8&&summary.points==8&&summary.missing==1&&summary.maximum==42.);REQUIRE(count(file,"difficulty_headers")==length+1);
 REQUIRE(!store.activate(anchor,anchor.tipHash).isEmpty());REQUIRE(store.summary(genesis).tipHash==forkAnchor.tipHash);
 auto unbound=forkAnchor;unbound.tipHash=hash(8);REQUIRE(!store.activate(unbound,forkAnchor.tipHash).isEmpty());
 DifficultyHistoryStore reopened(file);REQUIRE(reopened.summary(genesis).tipHash==forkAnchor.tipHash);REQUIRE(reopened.find(genesis,hash(length-1))->ancestryComplete);
 const auto unknownGenesis=hash(-1);auto unknownHeader=header(0,QJsonValue());unknownHeader.insert("hash",unknownGenesis);REQUIRE(store.record(owner,unknownGenesis,unknownGenesis,unknownHeader,utc).isEmpty());
 ChainObservation unknown{owner,unknownGenesis,0,utc,false,unknownGenesis};REQUIRE(store.activate(unknown,unknownGenesis).isEmpty());const auto absence=store.summary(unknownGenesis);REQUIRE(absence.available&&absence.points==0&&absence.missing==1&&!absence.minimum&&!absence.maximum);
 std::cout<<"PASS 5002 reverse headers, exact zero/unknown, persistence, hash/parent linkage, immutable conflicts and fork retention; finite fixture only, no live Core/network claim\n";
}
