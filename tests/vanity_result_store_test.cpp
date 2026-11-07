#include "wallet/vanity_result_store.h"
#include "wallet/vanity_address.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDateTime>
#include <QFileInfo>
#include <QUuid>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>
using pgn::wallet::VanityResultStore;
static void require(bool okay,const char* error){if(!okay)throw std::runtime_error(error);}
static QJsonObject result(int n){return {{"network","main"},{"session_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"address","PGN"+QString(25,'a')+QString(QChar('a'+n))},{"prefix","PGN"},{"suffix",""},{"created",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);QTemporaryDir dir;require(dir.isValid(),"Temporary store unavailable");const auto first=result(1),second=result(2);
 {
  const QString address="tpgnTXwARvekhiLGf3onheRMhn6SpPemPn9oP";
  require(pgn::wallet::isCanonicalTestnetKeyHashAlias(address),"Actual Core testnet alias refused");
  for(const auto& bad:{address+"1",address.left(36),address.left(36)+"0",address.left(36)+"1",address.toUpper(),QString("tpgn1qw508d6qejxtdg4y5r3zarvary0c5xw7kpmttkx")})
   require(!pgn::wallet::isCanonicalTestnetKeyHashAlias(bad),"Noncanonical testnet alias accepted");
  QTemporaryDir aliases;require(aliases.isValid(),"Alias index fixture unavailable");
  VanityResultStore store(aliases.path());
  auto current=result(9);current["network"]="test";current["address"]=address;current["prefix"]="tpgn";
  require(store.append(current,200).isEmpty(),"Canonical testnet alias was not indexed");
  auto legacy=result(10);legacy["network"]="test";legacy["address"]="tpgn1qw508d6qejxtdg4y5r3zarvary0c5xw7kpmttkx";legacy["prefix"]="tpgn1q";
  require(store.append(legacy,200).isEmpty()&&store.count()==2,"Old and new testnet records did not coexist");
  auto corrupt=result(11);corrupt["network"]="test";corrupt["address"]=address.left(36)+"0";corrupt["prefix"]="tpgn";
  require(!store.append(corrupt,200).isEmpty()&&store.count()==2,"Corrupt alias entered the address index");
 }
 {
  VanityResultStore store(dir.path(),65536);require(store.error().isEmpty(),"New store failed");
  require(store.append(first,200).isEmpty()&&store.count()==1&&store.reservedBytes()==32768,"First protected-key reservation wrong");
  require(store.sessionFor(first.value("address").toString(),"main")==first.value("session_id").toString()&&store.sessionFor(first.value("address").toString(),"test").isEmpty(),"Address lookup crossed network ownership");
  require(store.append(first,200).isEmpty()&&store.count()==1&&store.reservedBytes()==32768,"Duplicate result charged twice");
  auto conflict=first;conflict.insert("address",result(3).value("address"));require(!store.append(conflict,200).isEmpty()&&store.count()==1,"Conflicting identity overwrote an address");
  sqlite3* db{};const auto path=(dir.path()+"/addresses.sqlite").toUtf8();require(sqlite3_open(path.constData(),&db)==SQLITE_OK,"Fault injector unavailable");
  require(sqlite3_exec(db,"CREATE TRIGGER fail_budget BEFORE UPDATE ON budget BEGIN SELECT RAISE(ABORT,'test fault');END;",nullptr,nullptr,nullptr)==SQLITE_OK,"Fault injection failed");
  require(!store.append(second,200).isEmpty()&&store.count()==1&&store.reservedBytes()==32768,"Partial insert survived a budget-write failure");
  sqlite3_exec(db,"DROP TRIGGER fail_budget",nullptr,nullptr,nullptr);sqlite3_close(db);
  require(store.append(second,200).isEmpty()&&store.count()==2,"Write did not recover after fault removed");
  require(!store.append(result(4),200).isEmpty()&&store.count()==2&&store.reservedBytes()==65536,"Storage budget was exceeded");
  require(store.page(0,1).size()==1&&store.page(1,1).first().toObject()==first&&store.page(0,1001).isEmpty(),"Bounded stable pagination failed");
 }
 VanityResultStore reopened(dir.path(),65536);require(reopened.error().isEmpty()&&reopened.count()==2&&reopened.reservedBytes()==65536,"Address index did not reopen durably");
 require(!QFileInfo::exists(dir.path()+"/addresses.sqlite-wal")&&QFileInfo(dir.path()+"/addresses.sqlite").size()<16*1024*1024,"Unbounded WAL or oversized address index");
 QTemporaryDir patterns;auto matched=result(4);matched.insert("suffix",matched.value("address").toString().right(3));
 {VanityResultStore store(patterns.path());require(store.append(matched,200).isEmpty(),"Suffix result was refused");auto wrong=result(5);wrong.insert("suffix","ZZZ");require(!store.append(wrong,200).isEmpty()&&store.count()==1,"A mismatched suffix was saved");}
 {VanityResultStore store(patterns.path());require(store.page(0).first().toObject()==matched,"Suffix metadata did not survive reopening");}
 QTemporaryDir legacy;sqlite3* old{};auto legacyPath=(legacy.path()+"/addresses.sqlite").toUtf8();require(sqlite3_open(legacyPath.constData(),&old)==SQLITE_OK,"Legacy fixture unavailable");
 require(sqlite3_exec(old,"PRAGMA application_id=1346850390;PRAGMA user_version=2;CREATE TABLE addresses(seq INTEGER PRIMARY KEY,session TEXT UNIQUE NOT NULL,address TEXT UNIQUE NOT NULL,prefix TEXT NOT NULL,created TEXT NOT NULL,footprint INTEGER NOT NULL,network TEXT NOT NULL DEFAULT 'main');CREATE TABLE budget(id INTEGER PRIMARY KEY CHECK(id=1),reserved INTEGER NOT NULL CHECK(reserved>=0));INSERT INTO budget VALUES(1,0);",nullptr,nullptr,nullptr)==SQLITE_OK,"Legacy fixture creation failed");sqlite3_close(old);
 {VanityResultStore migrated(legacy.path());require(migrated.error().isEmpty()&&migrated.append(matched,200).isEmpty()&&migrated.page(0).first().toObject()==matched,"v2 pattern metadata migration failed");}
 std::cout<<"PASS vanity result metadata durability, dedup, collision refusal, rollback on injected write failure, pagination and bounded disk reservation\n";
}
