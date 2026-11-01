// Private closed fixtures only. No real source, wallet, GPU or network writes.
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
void Check(bool b,const char* what){++checks;if(!b){++failures;std::cerr<<"FAIL "<<what<<'\n';}}
void sql(const QString& path,const char* text){sqlite3* db{};if(sqlite3_open(path.toUtf8().constData(),&db)!=SQLITE_OK)throw std::runtime_error("fixture open");
    const auto rc=sqlite3_exec(db,text,nullptr,nullptr,nullptr);const std::string error=sqlite3_errmsg(db);sqlite3_close(db);if(rc!=SQLITE_OK)throw std::runtime_error(error);}
QByteArray hash(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture read");QCryptographicHash h(QCryptographicHash::Sha256);h.addData(&f);return h.result();}
void replaceProof(const QString& path,const QByteArray& bytes,const QByteArray& packed={}){
    sqlite3* db{};if(sqlite3_open(path.toUtf8().constData(),&db)!=SQLITE_OK)throw std::runtime_error("fixture replace");
    try{pgn::storage::ReadStatement s(db,"UPDATE source_gap_proofs SET canonical_bytes=?,canonical_sha256=?,zlib_proof=? WHERE gap_id=2");
        s.integer(1,bytes.size());s.text(2,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));const auto z=packed.isNull()?qCompress(bytes,1):packed;
        if(sqlite3_bind_blob(s.value,3,z.constData(),int(z.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("fixture bind");s.step();
    }catch(...){sqlite3_close(db);throw;}sqlite3_close(db);
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    QTemporaryDir dir;Check(dir.isValid(),"private fixture directory");const auto source=dir.path()+"/source.sqlite",journal=dir.path()+"/proof.sqlite";
    const QString run(32,'a');const SourceProofPolicy policy{4,{{128,32,32},10000},{10002,1048576,32,1}};
    sql(source,"CREATE TABLE native_source_identities(run TEXT PRIMARY KEY,identity TEXT NOT NULL);"
        "CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT NOT NULL,q TEXT NOT NULL);"
        "CREATE TABLE origins(gap_id INTEGER,run TEXT,source TEXT);CREATE INDEX native_origins_run_gap ON origins(run,gap_id);"
        "INSERT INTO native_source_identities VALUES('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','immutable declared identity');"
        "INSERT INTO gaps VALUES(1,'19','23'),(2,'199932000019059617039','199932000019059617887'),(3,'19','29');"
        "INSERT INTO origins SELECT id,'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','real fixture' FROM gaps;");
    const auto page=certifyNativeSourcePage(source,journal,run,policy);Check(page.error.isEmpty()&&page.verified==2&&page.rejected==1,"produce actual closed certificates with one rejected pair");
    const auto sourceHash=hash(source),journalHash=hash(journal);const auto positive=readCertifiedNativeSourceProof(source,journal,run,2,policy);
    Check(positive.evidence&&positive.error.isEmpty()&&positive.canonical.size()==2602&&positive.canonicalSha256=="d37396488a737832cc7d2e021e0681a88bf73154025a2120c43656278e3ea852","read actual847-position certificate, canonical roundtrip and independent verifier");
    Check(hash(source)==sourceHash&&hash(journal)==journalHash,"read touches neither database bytes nor cursor");
    const auto refused=[&](const QString& src,const QString& proof,const QString& r,qint64 id,SourceProofPolicy p){const auto v=readCertifiedNativeSourceProof(src,proof,r,id,p);return !v.error.isEmpty()&&!v.evidence&&v.canonical.isEmpty()&&v.canonicalSha256.isEmpty();};
    Check(refused(source,journal,run,3,policy),"stored negative is never promoted");
    Check(refused(source,journal,run,999,policy),"missing record refuses without proof");
    Check(refused(source,journal,QString(32,'b'),2,policy),"other run cannot claim source origin");
    Check(refused(source,source,run,2,policy),"same source and journal refused");
    const auto absent=dir.path()+"/missing.sqlite";Check(refused(source,absent,run,2,policy)&&!QFile::exists(absent),"missing journal creates nothing");
    std::atomic_bool cancel{true};const auto stopped=readCertifiedNativeSourceProof(source,journal,run,2,policy,&cancel);
    Check(stopped.cancelled&&!stopped.evidence&&stopped.canonical.isEmpty(),"pre-cancel exposes no proof");
    auto other=policy;--other.scientific.max_interior_positions;Check(refused(source,journal,run,2,other),"different semantic policy refuses positive reuse");
    other=policy;other.pageSize=1;Check(readCertifiedNativeSourceProof(source,journal,run,2,other).evidence.has_value(),"scheduling page size does not alter evidence identity");
    int serial{};const auto corrupt=[&](const char* mutation){const auto copy=dir.path()+"/corrupt"+QString::number(++serial)+".sqlite";if(!QFile::copy(journal,copy))throw std::runtime_error("copy fixture");
        sql(copy,"DROP TRIGGER proof_no_update;");sql(copy,mutation);Check(refused(source,copy,run,2,policy),mutation);};
    corrupt("UPDATE source_gap_proofs SET canonical_bytes=2147483647 WHERE gap_id=2");
    corrupt("UPDATE source_gap_proofs SET canonical_sha256='0000000000000000000000000000000000000000000000000000000000000000' WHERE gap_id=2");
    corrupt("UPDATE source_gap_proofs SET verdict=4294967296 WHERE gap_id=2");
    corrupt("UPDATE source_gap_proofs SET positions=846 WHERE gap_id=2");
    corrupt("UPDATE source_gap_proofs SET p='19',q='23' WHERE gap_id=2");
    corrupt("UPDATE source_gap_proofs SET zlib_proof=x'FFFFFFFF0102030405' WHERE gap_id=2");
    corrupt("UPDATE source_gap_proofs SET zlib_proof=zeroblob(2097152) WHERE gap_id=2");
    corrupt("UPDATE source_gap_proofs SET zlib_proof=CAST(zlib_proof AS TEXT) WHERE gap_id=2");
    corrupt("UPDATE proof_runs SET after_gap_id=1");
    corrupt("UPDATE proof_runs SET verified=100000");
    const auto malicious=[&](const QByteArray& bytes,const QByteArray& packed,const char* what){const auto copy=dir.path()+"/packed"+QString::number(++serial)+".sqlite";if(!QFile::copy(journal,copy))throw std::runtime_error("copy packed fixture");sql(copy,"DROP TRIGGER proof_no_update;");replaceProof(copy,bytes,packed);Check(refused(source,copy,run,2,policy),what);};
    auto trailing=qCompress(positive.canonical,1);trailing.append("junk");malicious(positive.canonical,trailing,"zlib trailing stream bytes refused even with correct canonical digest");
    auto overflow=qCompress(QByteArray(100000,'x'),1);const auto n=quint32(positive.canonical.size());for(int i=0;i<4;++i)overflow[i]=char(n>>(24-8*i));
    malicious(positive.canonical,overflow,"decompression expansion cannot exceed preallocated canonical budget");
    // A changed witness byte can still be a VALID alternative certificate;
    // the first attempted byte-flip poison correctly did not fail the reader.
    // Use an explicitly improper divisor instead: valid encoding, false math.
    auto falseEvidence=*positive.evidence;
    falseEvidence.interior.front()={pgn::core::CompositeWitnessKind::Divisor,falseEvidence.lower+1};
    const auto invalid=pgn::core::encodeCertifiedGap(falseEvidence,{policy.encoding,policy.scientific});
    const QByteArray falseBytes(reinterpret_cast<const char*>(invalid.data()),qsizetype(invalid.size()));
    malicious(falseBytes,{},"valid compressed bytes and matching SHA cannot replace native exact proof verification");
    const auto sourceCopy=dir.path()+"/changed-source.sqlite";Check(QFile::copy(source,sourceCopy),"source alteration fixture copy");
    sql(source,"UPDATE gaps SET q='199932000019059617889' WHERE id=2");Check(refused(source,journal,run,2,policy),"source pair changed after original verification refuses");
    sql(source,"UPDATE native_source_identities SET identity='another incarnation'");Check(refused(source,journal,run,2,policy),"source identity rebase refuses");
    Check(hash(journal)==journalHash,"all refused reader calls leave original journal unchanged");
    std::cout<<"SOURCE_PROOF_READ "<<checks<<" checks / "<<failures<<" failures; READONLY origin binding, bounded decompression, checksum+canonical+native independent reverification; NO network/GPU/wallet\n";
    return failures?1:0;
}catch(const std::exception& e){std::cerr<<"EXCEPTION "<<e.what()<<'\n';return 2;}}
