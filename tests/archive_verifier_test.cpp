#include "verification/archive_verifier.h"
#include "storage/sqlite_read.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <sqlite3.h>
#include <iostream>

namespace {
QByteArray digest(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
 QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};return hash.result();}
bool fixture(const QString& path){sqlite3* db=nullptr;if(sqlite3_open(path.toUtf8().constData(),&db)!=SQLITE_OK)return false;
 const auto okay=sqlite3_exec(db,"CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT NOT NULL,q TEXT NOT NULL);"
  "INSERT INTO gaps VALUES(1,'3','5'),(2,'7','11'),(3,'11','17'),"
  "(4,'318665857834031151167461','318665857834031151167463'),(5,'03','05'),(6,'15','17');",
  nullptr,nullptr,nullptr)==SQLITE_OK;sqlite3_close(db);return okay;}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);using namespace pgn::verification;
 QTemporaryDir dir;const auto source=dir.path()+"/supplied.sqlite",journal=dir.path()+"/proofs.sqlite";
 if(!fixture(source))return 1;const auto original=digest(source);const QString validator(64,'a');
 auto first=verifyArchivePage(source,journal,validator,{0,0,2,10000});
 if(!first.error.isEmpty()||first.processed!=2||first.evaluated!=2||first.cached||first.verified!=2||
    !first.more||first.after!=2||first.through!=6||digest(source)!=original)return 2;
 auto replay=verifyArchivePage(source,journal,validator,{0,6,2,10000});
 if(!replay.error.isEmpty()||replay.evaluated||replay.cached!=2||replay.verified!=2||replay.after!=2)return 3;
 auto rest=verifyArchivePage(source,journal,validator,{2,6,4,10000});
 if(!rest.error.isEmpty()||rest.processed!=4||rest.rejected!=4||rest.verified||rest.more||rest.after!=6)return 4;
 {
  pgn::storage::ReadOnlyDatabase db(journal);
  pgn::storage::ReadStatement rows(db.db,"SELECT COUNT(*),SUM(network_credit),SUM(range_coverage) FROM gap_verifications");rows.step();
  if(sqlite3_column_int(rows.value,0)!=6||sqlite3_column_int(rows.value,1)||sqlite3_column_int(rows.value,2))return 5;
  pgn::storage::ReadStatement interior(db.db,"SELECT first_interior_prime FROM gap_verifications WHERE source_id=3");interior.step();
  if(interior.text(0)!="13")return 6;
 }
 sqlite3* writing=nullptr;sqlite3_open(journal.toUtf8().constData(),&writing);
 if(sqlite3_exec(writing,"UPDATE gap_verifications SET checked_utc='fresh fake time'",nullptr,nullptr,nullptr)==SQLITE_OK||
    sqlite3_exec(writing,"DELETE FROM gap_verifications",nullptr,nullptr,nullptr)==SQLITE_OK){sqlite3_close(writing);return 7;}
 sqlite3_close(writing);
 auto lowBudget=verifyArchivePage(source,journal,validator,{1,2,1,0});
 if(!lowBudget.error.isEmpty()||lowBudget.verified||lowBudget.rejected!=1||lowBudget.evaluated!=1)return 8;
 const auto foreign=dir.path()+"/foreign.sqlite";if(!fixture(foreign))return 9;const auto foreignDigest=digest(foreign);
 if(verifyArchivePage(source,foreign,validator).error.isEmpty()||digest(foreign)!=foreignDigest||digest(source)!=original)return 10;
 if(verifyArchivePage(source,source,validator).error.isEmpty()||digest(source)!=original)return 11;
 std::atomic_bool cancelled{true};const auto neverCreated=dir.path()+"/cancelled.sqlite";
 const auto cancelledBatch=verifyArchivePage(source,neverCreated,validator,{},&cancelled);
 if(!cancelledBatch.cancelled||cancelledBatch.processed||QFile::exists(neverCreated))return 12;
 if(verifyArchivePage(source,neverCreated,validator,{0,99,16,10000}).error.isEmpty()||QFile::exists(neverCreated))return 13;
 if(verifyArchivePage(source,neverCreated,"missing-hash").error.isEmpty()||QFile::exists(neverCreated))return 14;
 auto changedValidator=verifyArchivePage(source,journal,QString(64,'b'),{0,1,1,10000});
 if(!changedValidator.error.isEmpty()||changedValidator.evaluated!=1||changedValidator.cached)return 15;
 if(digest(source)!=original)return 16;
 std::cout<<"PASS finite read-only supplied archive -> append-only exact independent journal; watermark/cache/restart/validator/budget/cancel/foreign-target guards; NO MINING/COVERAGE/NETWORK CREDIT\n";
}
