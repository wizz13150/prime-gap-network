// Closed local SQLite fixtures. No real source/history/UI/GPU/network access.
#include "verification/source_proof_page.h"
#include "storage/sqlite_read.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <iostream>

using namespace pgn::verification;
namespace {
unsigned checks{},failures{};
void Check(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
void exec(const QString& path,const char* sql){sqlite3* db=nullptr;if(sqlite3_open(path.toUtf8().constData(),&db)!=SQLITE_OK)throw std::runtime_error("fixture open");
    const auto code=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);const std::string error=sqlite3_errmsg(db);sqlite3_close(db);if(code!=SQLITE_OK)throw std::runtime_error(error);}
QByteArray digest(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&file))return {};return h.result();}
qint64 count(const QString& journal){pgn::storage::ReadOnlyDatabase db(journal);pgn::storage::ReadStatement s(db.db,"SELECT COUNT(*) FROM source_gap_proofs");s.step();return sqlite3_column_int64(s.value,0);}
void fixture(const QString& source){exec(source,
    "CREATE TABLE native_source_identities(run TEXT PRIMARY KEY,identity TEXT NOT NULL);"
    "CREATE TABLE origins(gap_id INTEGER NOT NULL,run TEXT NOT NULL,source TEXT NOT NULL,UNIQUE(gap_id,run,source));"
    "CREATE INDEX native_origins_run_gap ON origins(run,gap_id);"
    "CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT NOT NULL,q TEXT NOT NULL);"
    "INSERT INTO native_source_identities VALUES('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','exact declared source identity A'),('bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb','exact declared source identity B');"
    "INSERT INTO gaps VALUES(1,'19','23'),(2,'3','5'),(3,'19','29'),(4,'199932000019059617039','199932000019059617887'),"
    "(5,'318665857834031151167461','318665857834031151167463'),(6,'03','05'),(7,'2179','2203');"
    "INSERT INTO origins SELECT id,'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','sourceA' FROM gaps WHERE id<=6;"
    "INSERT INTO origins VALUES(4,'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','same pair second declared origin'),(7,'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb','sourceB');");}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    QTemporaryDir dir;if(!dir.isValid())return 2;const auto source=dir.path()+"/source.sqlite",journal=dir.path()+"/proofs.sqlite";
    fixture(source);const auto original=digest(source);const QString run(32,'a');
    const SourceProofPolicy policy{2,{{128,32,32},1000},{1000,65536,32,1000}};
    auto first=certifyNativeSourcePage(source,journal,run,policy);
    Check(first.error.isEmpty()&&first.processed==2&&first.verified==2&&first.after==2&&first.total==2&&first.more&&first.through==6,"first bounded page sees ONLY requested run and exact watermark");
    Check(count(journal)==2&&digest(source)==original,"full proofs published separately; source byte-identical");
    exec(journal,"CREATE TRIGGER fail_cursor BEFORE UPDATE ON proof_runs BEGIN SELECT RAISE(ABORT,'injected cursor failure'); END;");
    auto failed=certifyNativeSourcePage(source,journal,run,policy);
    Check(!failed.error.isEmpty()&&!failed.processed&&failed.after==2&&count(journal)==2,"cursor commit failure rolls back new proof row; no partial advance/duplicate");
    exec(journal,"DROP TRIGGER fail_cursor;");
    auto second=certifyNativeSourcePage(source,journal,run,policy);
    Check(second.error.isEmpty()&&second.processed==2&&second.verified==1&&second.rejected==1&&second.after==4&&second.total==4&&second.more,"restart resumes exact uncommitted pair; duplicate origins do not duplicate work");
    auto third=certifyNativeSourcePage(source,journal,run,policy);
    Check(third.error.isEmpty()&&third.processed==2&&third.rejected==2&&third.after==6&&third.total==6&&third.verifiedTotal==3&&third.rejectedTotal==3&&!third.more,"unsupported/invalid are durable negative outcomes, never partial positive");
    auto idle=certifyNativeSourcePage(source,journal,run,policy);
    Check(idle.error.isEmpty()&&!idle.processed&&idle.after==6&&count(journal)==6,"reopen at EOF neither repeats nor retimestamps prior results");
    {
        pgn::storage::ReadOnlyDatabase db(journal);
        pgn::storage::ReadStatement proof(db.db,"SELECT canonical_bytes,canonical_sha256,zlib_proof,network_credit,range_coverage FROM source_gap_proofs WHERE gap_id=4");proof.step();
        const auto size=sqlite3_column_int(proof.value,0);const auto* blob=static_cast<const char*>(sqlite3_column_blob(proof.value,2));
        const QByteArray packed(blob,sqlite3_column_bytes(proof.value,2));const auto bytes=qUncompress(packed);
        Check(size==2602&&bytes.size()==size&&packed.size()<size&&proof.text(1)=="d37396488a737832cc7d2e021e0681a88bf73154025a2120c43656278e3ea852","stored real-source proof compresses losslessly without changing canonical bytes/hash");
        const auto actual=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        Check(actual==proof.text(1),"decompressed certificate checksum independently checked");
        const pgn::core::Bytes encoded(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size());
        const auto decoded=pgn::core::decodeCertifiedGap(encoded,{policy.encoding,policy.scientific});
        Check(pgn::core::verifyCertifiedGap(decoded,policy.scientific).verdict==pgn::core::CertifiedGapVerdict::VerifiedConsecutiveGap,"stored bytes decode and independently certify all847 positions");
        Check(!sqlite3_column_int(proof.value,3)&&!sqlite3_column_int(proof.value,4),"local full pair proof grants neither network credit nor interval coverage");
    }
    bool refused=false;try{exec(journal,"UPDATE source_gap_proofs SET checked_utc='fake new time'");}catch(...){refused=true;}Check(refused,"immutable proof evidence rejects update");
    refused=false;try{exec(journal,"DELETE FROM source_gap_proofs");}catch(...){refused=true;}Check(refused,"immutable proof evidence rejects deletion");
    auto low=policy;low.scientific.max_interior_positions=846;low.pageSize=4;
    auto limited=certifyNativeSourcePage(source,journal,run,low);
    Check(limited.error.isEmpty()&&limited.after==4&&limited.total==4&&limited.verifiedTotal==2&&limited.rejectedTotal==2&&limited.policySha256!=first.policySha256,"different explicit proof budget has separate cursor/outcomes; cannot reuse positive across budgets");
    auto b=certifyNativeSourcePage(source,journal,QString(32,'b'),policy);
    Check(b.error.isEmpty()&&b.processed==1&&b.verified==1&&b.after==7&&b.total==1,"second source run has independent durable cursor");
    Check(digest(source)==original,"all normal page/retry/cache/budget calls leave actual source untouched");
    const auto before=count(journal);auto bad=certifyNativeSourcePage(source,source,run,policy);
    Check(!bad.error.isEmpty()&&count(journal)==before&&digest(source)==original,"same source/target identity rejected");
    const auto foreign=dir.path()+"/foreign.sqlite";fixture(foreign);const auto foreignHash=digest(foreign);
    bad=certifyNativeSourcePage(source,foreign,run,policy);Check(!bad.error.isEmpty()&&digest(foreign)==foreignHash,"foreign nonempty output refused unchanged");
    std::atomic_bool cancelled{true};const auto cancelledPath=dir.path()+"/never.sqlite";
    auto never=certifyNativeSourcePage(source,cancelledPath,run,policy,&cancelled);
    Check(never.cancelled&&!never.processed&&!QFile::exists(cancelledPath),"pre-cancel creates no proof journal or positive result");
    bad=certifyNativeSourcePage(source,cancelledPath,QString(32,'c'),policy);
    Check(!bad.error.isEmpty()&&!QFile::exists(cancelledPath),"no enrollment without a declared native source identity");
    const auto empty=dir.path()+"/interrupted-init.sqlite";exec(empty,"PRAGMA user_version=0;");
    auto init=certifyNativeSourcePage(source,empty,run,policy);
    Check(init.error.isEmpty()&&init.processed==2,"empty interrupted journal initialization safely resumes, no useful file overwritten");
    exec(source,"UPDATE native_source_identities SET identity='different incarnation' WHERE run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';");
    bad=certifyNativeSourcePage(source,journal,run,policy);
    Check(!bad.error.isEmpty()&&count(journal)==before,"changed declared identity cannot rebind existing proof cursor");
    exec(source,"UPDATE native_source_identities SET identity='exact declared source identity A' WHERE run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'; DELETE FROM origins WHERE gap_id=6;");
    bad=certifyNativeSourcePage(source,journal,run,policy);
    Check(!bad.error.isEmpty()&&count(journal)==before,"regressed source watermark cannot appear fully verified");
    std::cout<<"SOURCE_PROOF_PAGE "<<checks<<" checks / "<<failures<<" failures; actual full847-position certificate, indexed finite pages, atomic durable cursor, rollback/restart/compression/budget/identity/cancel; NO userDB/GPU/network/mining\n";
    return failures?1:0;
}catch(const std::exception& e){std::cerr<<"EXCEPTION "<<e.what()<<'\n';return 2;}}
