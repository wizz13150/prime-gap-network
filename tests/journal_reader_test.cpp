#include "verification/journal_reader.h"
#include "verification/local_validator_pins.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFileInfo>
#include <sqlite3.h>
#include <iostream>

using namespace pgn::verification;
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temporary;
    const QString file=temporary.filePath("independent.sqlite"),sha(64,'a');
    if(readLatestReceipt(file,{sha}).error.isEmpty()||QFileInfo::exists(file))return 1;
    sqlite3* db{};sqlite3_open(file.toUtf8().constData(),&db);
    const auto run=[db](const char* sql){return sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK;};
    if(!run("CREATE TABLE gap_verifications(id INTEGER PRIMARY KEY,source_id INTEGER,source_archive TEXT,p TEXT,q TEXT,method TEXT,strict_bound TEXT,validator_sha256 TEXT,odd_budget INTEGER,verdict INTEGER,checked_interior INTEGER,elapsed_ns INTEGER,checked_utc TEXT,network_credit INTEGER,range_coverage INTEGER)"))return 2;
    if(readLatestReceipt(file,{sha}).error.isEmpty())return 3;
    if(!run("PRAGMA application_id=1346850390;PRAGMA user_version=1"))return 4;
    auto empty=readLatestReceipt(file,{sha});if(!empty.error.isEmpty()||empty.receipt)return 5;
    const auto insert=QString("INSERT INTO gap_verifications VALUES(1,7,'source.sqlite','11','13','MR12_STRICT_SW2015_BOUND','318665857834031151167461','%1',10000,0,0,0,'2026-10-11T12:00:00.000Z',0,0)").arg(sha);
    if(!run(insert.toUtf8().constData()))return 6;
    auto actual=readLatestReceipt(file,{sha});
    if(!actual.error.isEmpty()||!actual.receipt||actual.receipt->id!=1||actual.receipt->sourceId!=7||
       actual.receipt->elapsedNs!=0||actual.receipt->originalCheckedUtc.toString(Qt::ISODateWithMs)!="2026-10-11T12:00:00.000Z")return 7;
    if(readLatestReceipt(file,{QString(64,'b')}).error.isEmpty())return 8;
    if(!run("UPDATE gap_verifications SET network_credit=1"))return 9;
    if(readLatestReceipt(file,{sha}).error.isEmpty())return 10;
    if(!run("UPDATE gap_verifications SET network_credit=0,checked_interior=1"))return 11;
    if(readLatestReceipt(file,{sha}).error.isEmpty())return 12;
    if(!run("UPDATE gap_verifications SET checked_interior=0,p='4',q='6'"))return 13;
    if(readLatestReceipt(file,{sha}).error.isEmpty())return 14;
    if(!run("UPDATE gap_verifications SET p='11',q='17',verdict=2,checked_interior=1"))return 15;
    auto failed=readLatestReceipt(file,{sha});if(!failed.receipt||failed.receipt->verdict!=GapVerdict::InteriorPrime)return 16;
    if(!run("UPDATE gap_verifications SET checked_utc='2026-10-11T12:00:00+02:00'"))return 17;
    if(readLatestReceipt(file,{sha}).error.isEmpty())return 18;
    // Reader is strictly read-only even for malformed evidence; the test fixture
    // intentionally lacks append-only triggers so negative mutations are possible.
    sqlite3_close(db);
    if(app.arguments().size()==2){
        auto production=readLatestReceipt(app.arguments()[1],localArchiveValidatorPins());
        if(!production.error.isEmpty()||!production.receipt)return 19;
        std::cout<<"Actual indexed receipt id="<<production.receipt->id<<" source="<<production.receipt->sourceId
                 <<" originalUTC="<<production.receipt->originalCheckedUtc.toString(Qt::ISODateWithMs).toStdString()
                 <<" verdict="<<int(production.receipt->verdict)<<"; no network credit or range coverage\n";
    }
    std::cout<<"PASS qualified last-receipt read-only/domain/originalUTC/zero/negative guards\n";
}
