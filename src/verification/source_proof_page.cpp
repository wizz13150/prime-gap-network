#include "verification/source_proof_page.h"
#include "storage/sqlite_read.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <limits>
#include <memory>
#include <zlib.h>

namespace pgn::verification {
namespace {
using Statement=storage::ReadStatement;
constexpr int APP_ID=0x50474e43;
struct Pair {qint64 id;QString p,q;};
struct Cursor {qint64 after{},total{},verified{},rejected{};};
[[noreturn]]void fail(const char* text){throw std::runtime_error(text);}
QString sha(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
pgn::core::Integer parseCanonicalDecimal(const QString& text){
    // Caller already checked canonical digits and an explicit byte budget.
    // No permissive Boost string/octal parser or GCC16 parser-overread path.
    pgn::core::Integer value{};for(const auto c:text){value*=10;value+=c.unicode()-'0';}return value;
}
QString policyIdentity(const SourceProofPolicy& p){
    QStringList parts{"PGN_NATIVE_CERTIFIED_GAP_MR12_V1"};
    for(const auto value:std::initializer_list<quint64>{quint64(p.scientific.endpoint_limits.max_integer_bits),quint64(p.scientific.endpoint_limits.max_nodes),
        quint64(p.scientific.endpoint_limits.max_factors_per_node),p.scientific.max_interior_positions,p.encoding.max_records,
        p.encoding.max_encoded_bytes,quint64(p.encoding.max_integer_bytes),quint64(p.encoding.max_segments)})parts.append(QString::number(value));
    return sha(parts.join('|').toUtf8());
}
bool validateJournal(sqlite3* db){
    Statement id(db,"PRAGMA application_id"),version(db,"PRAGMA user_version");id.step();version.step();
    if(sqlite3_column_int(id.value,0)==APP_ID&&sqlite3_column_int(version.value,0)==1)return true;
    if(sqlite3_column_int(id.value,0)==0&&sqlite3_column_int(version.value,0)==0){
        Statement tables(db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table'");tables.step();
        if(sqlite3_column_int64(tables.value,0)==0)return false; // Interrupted empty initialization, no data to overwrite.
    }
    fail("Not a native source proof journal; left unchanged");
}
Cursor cursor(sqlite3* db,const QString& source,const QString& run,const QString& policy,const QString& identity){
    Statement s(db,"SELECT identity_sha256,after_gap_id,total,verified,rejected FROM proof_runs WHERE source=? AND run=? AND policy_sha256=?");
    s.text(1,source);s.text(2,run);s.text(3,policy);if(s.step()!=SQLITE_ROW)return {};
    if(s.text(0)!=identity)fail("Source identity changed; proof cursor cannot silently rebase");
    Cursor c{sqlite3_column_int64(s.value,1),sqlite3_column_int64(s.value,2),sqlite3_column_int64(s.value,3),sqlite3_column_int64(s.value,4)};
    if(c.after<0||c.total<0||c.verified<0||c.rejected<0||c.verified>c.total||c.rejected!=c.total-c.verified)
        fail("Native proof cursor/counters invalid; no promotion");
    return c;
}
class Journal {
public:
    sqlite3* db{};
    explicit Journal(const QString& path){
        if(QFileInfo(path).exists()){storage::ReadOnlyDatabase old(path);validateJournal(old.db);}
        if(!QDir().mkpath(QFileInfo(path).absolutePath()))fail("Proof journal directory unavailable");
        if(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){
            if(db)sqlite3_close(db);db=nullptr;fail("Native proof journal unavailable");
        }
        sqlite3_busy_timeout(db,1000);
        try{execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; BEGIN IMMEDIATE;"
            "CREATE TABLE IF NOT EXISTS proof_runs(source TEXT NOT NULL,run TEXT NOT NULL,policy_sha256 TEXT NOT NULL,identity_sha256 TEXT NOT NULL,"
            "after_gap_id INTEGER NOT NULL,total INTEGER NOT NULL,verified INTEGER NOT NULL,rejected INTEGER NOT NULL,PRIMARY KEY(source,run,policy_sha256)) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS source_gap_proofs(source TEXT NOT NULL,run TEXT NOT NULL,policy_sha256 TEXT NOT NULL,gap_id INTEGER NOT NULL,"
            "identity_sha256 TEXT NOT NULL,p TEXT NOT NULL,q TEXT NOT NULL,verdict INTEGER NOT NULL,positions INTEGER NOT NULL,canonical_bytes INTEGER NOT NULL,"
            "canonical_sha256 TEXT NOT NULL,zlib_proof BLOB NOT NULL,checked_utc TEXT NOT NULL,network_credit INTEGER NOT NULL CHECK(network_credit=0),"
            "range_coverage INTEGER NOT NULL CHECK(range_coverage=0),PRIMARY KEY(source,run,policy_sha256,gap_id)) WITHOUT ROWID;"
            "CREATE TRIGGER IF NOT EXISTS proof_no_update BEFORE UPDATE ON source_gap_proofs BEGIN SELECT RAISE(ABORT,'Proof evidence is append-only'); END;"
            "CREATE TRIGGER IF NOT EXISTS proof_no_delete BEFORE DELETE ON source_gap_proofs BEGIN SELECT RAISE(ABORT,'Proof evidence is append-only'); END;"
            "PRAGMA application_id=0x50474e43; PRAGMA user_version=1; COMMIT;");
        }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);db=nullptr;throw;}
    }
    ~Journal(){if(db)sqlite3_close(db);}
    void execute(const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    Cursor append(const QString& source,const QString& run,const QString& policy,const QString& identity,const Pair& pair,
        const Cursor& expected,pgn::core::GapBuildVerdict verdict,std::uint64_t positions,const QByteArray& bytes){
        if(expected.total==std::numeric_limits<qint64>::max())fail("SQLite proof counter exhausted; source retained");
        const bool positive=verdict==pgn::core::GapBuildVerdict::Built;
        if(positive==bytes.isEmpty()||positions>quint64(std::numeric_limits<qint64>::max()))fail("Incomplete native proof result");
        const auto packed=positive?qCompress(bytes,1):QByteArray{};
        execute("BEGIN IMMEDIATE");
        try{
            const auto old=cursor(db,source,run,policy,identity);
            if(old.after!=expected.after||old.total!=expected.total||old.verified!=expected.verified||old.rejected!=expected.rejected||pair.id<=old.after)
                fail("Proof cursor changed concurrently; no duplicate publication");
            Statement add(db,"INSERT INTO source_gap_proofs VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,0,0)");
            add.text(1,source);add.text(2,run);add.text(3,policy);add.integer(4,pair.id);add.text(5,identity);add.text(6,pair.p);add.text(7,pair.q);
            add.integer(8,int(verdict));add.integer(9,qint64(positions));add.integer(10,bytes.size());add.text(11,positive?sha(bytes):QString{});
            if(sqlite3_bind_blob(add.value,12,packed.constData(),int(packed.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));
            add.text(13,QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));add.step();
            Cursor next{pair.id,old.total+1,old.verified+int(positive),old.rejected+int(!positive)};
            Statement update(db,"INSERT INTO proof_runs VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(source,run,policy_sha256) DO UPDATE SET "
                "after_gap_id=excluded.after_gap_id,total=excluded.total,verified=excluded.verified,rejected=excluded.rejected");
            update.text(1,source);update.text(2,run);update.text(3,policy);update.text(4,identity);update.integer(5,next.after);
            update.integer(6,next.total);update.integer(7,next.verified);update.integer(8,next.rejected);update.step();
            execute("COMMIT");return next;
        }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
};
}
SourceProofPage certifyNativeSourcePage(const QString& input,const QString& output,const QString& run,SourceProofPolicy policy,const std::atomic_bool* cancel){
    SourceProofPage result;result.run=run;
    const auto cancelled=[&]{return cancel&&cancel->load(std::memory_order_relaxed);};
    try{
        if(cancelled()){result.cancelled=true;return result;}
        static const QRegularExpression uuid("^[0-9a-f]{32}$"),decimal("^(0|[1-9][0-9]*)$");
        if(!uuid.match(run).hasMatch()||policy.pageSize<1||policy.pageSize>64||!policy.encoding.max_integer_bytes||
            !policy.encoding.max_encoded_bytes||policy.encoding.max_encoded_bytes>quint64(std::numeric_limits<int>::max()))fail("Explicit finite source proof policy invalid");
        result.policySha256=policyIdentity(policy);
        const QFileInfo src(input),out(output);const auto source=src.canonicalFilePath(),target=out.absoluteFilePath();
        if(source.isEmpty()||src.isSymLink()||out.isSymLink()||source.compare(target,Qt::CaseInsensitive)==0||
            source.compare(out.canonicalFilePath(),Qt::CaseInsensitive)==0)fail("Proof source and journal must be distinct existing identities");
        QVector<Pair> pairs;Cursor current;
        {
            storage::ReadOnlyDatabase db(source);
            sqlite3_progress_handler(db.db,1000,[](void* p)->int{return p&&static_cast<const std::atomic_bool*>(p)->load(std::memory_order_relaxed);},const_cast<std::atomic_bool*>(cancel));
            db.execute("BEGIN");
            Statement identity(db.db,"SELECT identity FROM native_source_identities WHERE run=?");identity.text(1,run);
            if(identity.step()!=SQLITE_ROW||sqlite3_column_type(identity.value,0)!=SQLITE_TEXT||sqlite3_column_bytes(identity.value,0)<1||sqlite3_column_bytes(identity.value,0)>16384)
                fail("Declared native source identity missing/unbounded; no proof cursor enrollment");
            result.identitySha256=sha(identity.text(0).toUtf8());
            if(out.exists()){storage::ReadOnlyDatabase existing(target);if(validateJournal(existing.db))current=cursor(existing.db,source,run,result.policySha256,result.identitySha256);}
            result.after=current.after;result.total=current.total;result.verifiedTotal=current.verified;result.rejectedTotal=current.rejected;
            Statement last(db.db,"SELECT gap_id FROM origins WHERE run=? ORDER BY gap_id DESC LIMIT 1");last.text(1,run);
            if(last.step()==SQLITE_ROW)result.through=sqlite3_column_int64(last.value,0);
            if(result.through<current.after)fail("Native source origin watermark regressed; source/journal unchanged");
            // LIMIT is INSIDE the ordered covering-index subquery. DISTINCT on
            // the large joined result would instead scan/sort the whole run.
            Statement page(db.db,"SELECT a.gap_id,g.p,g.q FROM (SELECT DISTINCT gap_id FROM origins WHERE run=? AND gap_id>? "
                "ORDER BY gap_id LIMIT ?) a LEFT JOIN gaps g ON g.id=a.gap_id ORDER BY a.gap_id");
            page.text(1,run);page.integer(2,current.after);page.integer(3,policy.pageSize+1);
            while(page.step()==SQLITE_ROW){
                if(pairs.size()==policy.pageSize){result.more=true;break;}
                if(sqlite3_column_type(page.value,0)!=SQLITE_INTEGER||sqlite3_column_type(page.value,1)!=SQLITE_TEXT||sqlite3_column_type(page.value,2)!=SQLITE_TEXT||
                    sqlite3_column_bytes(page.value,1)>qint64(policy.encoding.max_integer_bytes)*3+1||sqlite3_column_bytes(page.value,2)>qint64(policy.encoding.max_integer_bytes)*3+1)
                    fail("Native origin pair missing or exceeds explicit transport budget");
                pairs.append({sqlite3_column_int64(page.value,0),page.text(1),page.text(2)});
            }
            db.execute("COMMIT");
        }
        if(cancelled()){result.cancelled=true;return result;}
        if(pairs.isEmpty())return result;
        Journal journal(target);
        for(const auto& pair:pairs){
            if(cancelled()){result.cancelled=true;break;}
            pgn::core::GapBuildResult built{pgn::core::GapBuildVerdict::InvalidPair,{},0};QByteArray bytes;
            if(decimal.match(pair.p).hasMatch()&&decimal.match(pair.q).hasMatch()){
                const auto p=parseCanonicalDecimal(pair.p),q=parseCanonicalDecimal(pair.q);
                built=pgn::core::buildBoundedCertifiedGap(p,q-p,policy.scientific,cancelled);
                if(built.verdict==pgn::core::GapBuildVerdict::Built){
                    if(!built.evidence||pgn::core::verifyCertifiedGap(*built.evidence,policy.scientific).verdict!=pgn::core::CertifiedGapVerdict::VerifiedConsecutiveGap)
                        fail("Produced certificate failed independent native verifier; no publication");
                    const auto encoded=pgn::core::encodeCertifiedGap(*built.evidence,{policy.encoding,policy.scientific});
                    bytes=QByteArray(reinterpret_cast<const char*>(encoded.data()),qsizetype(encoded.size()));
                }
            }
            if(built.verdict==pgn::core::GapBuildVerdict::Cancelled||cancelled()){result.cancelled=true;break;}
            current=journal.append(source,run,result.policySha256,result.identitySha256,pair,current,built.verdict,built.interior_positions_built,bytes);
            ++result.processed;if(built.verdict==pgn::core::GapBuildVerdict::Built)++result.verified;else ++result.rejected;
            result.after=current.after;result.total=current.total;result.verifiedTotal=current.verified;result.rejectedTotal=current.rejected;
        }
    }catch(const std::exception& e){if(cancelled())result.cancelled=true;else result.error=QString::fromUtf8(e.what()).left(512);}
    return result;
}
SourceProofRead readCertifiedNativeSourceProof(const QString& input,const QString& output,const QString& run,qint64 gapId,SourceProofPolicy policy,const std::atomic_bool* cancel){
    SourceProofRead result;result.run=run;result.gapId=gapId;
    const auto cancelled=[&]{return cancel&&cancel->load(std::memory_order_relaxed);};
    try{
        if(cancelled()){result.cancelled=true;return result;}
        static const QRegularExpression uuid("^[0-9a-f]{32}$"),decimal("^(0|[1-9][0-9]*)$"),digest("^[0-9a-f]{64}$");
        if(!uuid.match(run).hasMatch()||gapId<=0||!policy.encoding.max_integer_bytes||!policy.encoding.max_encoded_bytes||
            policy.encoding.max_encoded_bytes>quint64(std::numeric_limits<int>::max()-1048576))fail("Explicit bounded proof read policy invalid");
        result.policySha256=policyIdentity(policy);
        const QFileInfo src(input),out(output);const auto source=src.canonicalFilePath(),journal=out.canonicalFilePath();
        if(source.isEmpty()||journal.isEmpty()||src.isSymLink()||out.isSymLink()||source.compare(journal,Qt::CaseInsensitive)==0)
            fail("Proof read requires distinct existing source/journal identities");
        storage::ReadOnlyDatabase db(source),proofDb(journal);
        db.execute("PRAGMA query_only=ON; PRAGMA trusted_schema=OFF; BEGIN");proofDb.execute("PRAGMA query_only=ON; PRAGMA trusted_schema=OFF; BEGIN");
        if(!validateJournal(proofDb.db))fail("Empty proof journal is not certified evidence");
        Statement identity(db.db,"SELECT identity FROM native_source_identities WHERE run=?");identity.text(1,run);
        if(identity.step()!=SQLITE_ROW||sqlite3_column_type(identity.value,0)!=SQLITE_TEXT||sqlite3_column_bytes(identity.value,0)<1||sqlite3_column_bytes(identity.value,0)>16384)
            fail("Declared source identity unavailable or unbounded");
        result.identitySha256=sha(identity.text(0).toUtf8());
        const auto state=cursor(proofDb.db,source,run,result.policySha256,result.identitySha256);
        if(gapId>state.after||!state.verified)fail("Proof is outside the durable verified cursor");
        Statement origin(db.db,"SELECT p,q FROM gaps WHERE id=? AND EXISTS(SELECT 1 FROM origins WHERE run=? AND gap_id=?)");
        origin.integer(1,gapId);origin.text(2,run);origin.integer(3,gapId);
        if(origin.step()!=SQLITE_ROW)fail("Requested pair is not a member of this declared native source");
        const auto decimalBudget=qint64(policy.encoding.max_integer_bytes)*3+1;
        for(int col=0;col<2;++col)if(sqlite3_column_type(origin.value,col)!=SQLITE_TEXT||sqlite3_column_bytes(origin.value,col)>decimalBudget)
            fail("Source pair exceeds explicit read budget");
        const auto p=origin.text(0),q=origin.text(1);
        if(!decimal.match(p).hasMatch()||!decimal.match(q).hasMatch())fail("Source pair is not canonical decimal");
        // Metadata first: length(blob) does not materialize an attacker-sized
        // blob in our process. The second lookup uses the SAME read snapshot.
        Statement meta(proofDb.db,"SELECT identity_sha256,p,q,verdict,positions,canonical_bytes,canonical_sha256,network_credit,range_coverage,length(zlib_proof),typeof(zlib_proof) "
            "FROM source_gap_proofs WHERE source=? AND run=? AND policy_sha256=? AND gap_id=?");
        meta.text(1,source);meta.text(2,run);meta.text(3,result.policySha256);meta.integer(4,gapId);
        if(meta.step()!=SQLITE_ROW)fail("Stored certificate missing for this exact source/policy/record");
        for(int col:{0,1,2,6})if(sqlite3_column_type(meta.value,col)!=SQLITE_TEXT||sqlite3_column_bytes(meta.value,col)>(col==1||col==2?decimalBudget:64))
            fail("Stored certificate metadata has invalid type or size");
        for(int col:{3,4,5,7,8,9})if(sqlite3_column_type(meta.value,col)!=SQLITE_INTEGER)fail("Stored certificate integer metadata invalid");
        const auto size=sqlite3_column_int64(meta.value,5),packedSize=sqlite3_column_int64(meta.value,9);
        if(meta.text(0)!=result.identitySha256||meta.text(1)!=p||meta.text(2)!=q||sqlite3_column_int64(meta.value,3)!=int(pgn::core::GapBuildVerdict::Built)||
            sqlite3_column_int64(meta.value,7)||sqlite3_column_int64(meta.value,8)||meta.text(10)!="blob"||size<=0||quint64(size)>policy.encoding.max_encoded_bytes||
            packedSize<=4||quint64(packedSize)>quint64(compressBound(uLong(size)))+4||!digest.match(meta.text(6)).hasMatch())
            fail("Stored certificate scope/identity/size/verdict invalid");
        const auto expectedSha=meta.text(6);const auto positions=sqlite3_column_int64(meta.value,4);
        Statement blob(proofDb.db,"SELECT zlib_proof FROM source_gap_proofs WHERE source=? AND run=? AND policy_sha256=? AND gap_id=?");
        blob.text(1,source);blob.text(2,run);blob.text(3,result.policySha256);blob.integer(4,gapId);
        if(blob.step()!=SQLITE_ROW||sqlite3_column_type(blob.value,0)!=SQLITE_BLOB||sqlite3_column_bytes(blob.value,0)!=packedSize)
            fail("Stored compressed certificate changed or malformed");
        const auto* packed=static_cast<const unsigned char*>(sqlite3_column_blob(blob.value,0));
        const quint64 declared=(quint64(packed[0])<<24)|(quint64(packed[1])<<16)|(quint64(packed[2])<<8)|quint64(packed[3]);
        if(declared!=quint64(size))fail("Compressed certificate header disagrees with bounded canonical size");
        QByteArray canonical(qsizetype(size),'\0');z_stream stream{};
        stream.next_in=const_cast<Bytef*>(packed+4);stream.avail_in=uInt(packedSize-4);
        stream.next_out=reinterpret_cast<Bytef*>(canonical.data());stream.avail_out=uInt(size);
        if(inflateInit(&stream)!=Z_OK)fail("Certificate decompressor unavailable");
        const auto inflated=inflate(&stream,Z_FINISH);const bool exact=inflated==Z_STREAM_END&&stream.total_in==uLong(packedSize-4)&&stream.total_out==uLong(size);
        inflateEnd(&stream);
        if(!exact||sha(canonical)!=expectedSha)fail("Compressed certificate overflow/trailing bytes/corruption/digest mismatch");
        if(cancelled()){result.cancelled=true;return result;}
        const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(canonical.constData()),size_t(canonical.size()));
        auto evidence=pgn::core::decodeCertifiedGap(bytes,{policy.encoding,policy.scientific});
        if(evidence.lower!=parseCanonicalDecimal(p)||evidence.lower+evidence.gap!=parseCanonicalDecimal(q)||
            evidence.gap-1!=positions||pgn::core::verifyCertifiedGap(evidence,policy.scientific).verdict!=pgn::core::CertifiedGapVerdict::VerifiedConsecutiveGap)
            fail("Stored certificate fails source binding or independent mathematical verification");
        const auto roundtrip=pgn::core::encodeCertifiedGap(evidence,{policy.encoding,policy.scientific});
        if(roundtrip.size()!=size_t(canonical.size())||!std::equal(roundtrip.begin(),roundtrip.end(),bytes.begin()))fail("Stored certificate is not canonical");
        if(cancelled()){result.cancelled=true;return result;}
        result.canonicalSha256=expectedSha;result.canonical=std::move(canonical);result.evidence=std::move(evidence);
    }catch(const std::exception& e){result.canonical.clear();result.evidence.reset();result.canonicalSha256.clear();
        if(cancelled())result.cancelled=true;else result.error=QString::fromUtf8(e.what()).left(512);}
    return result;
}
SourceProofScan scanNativeSourceProofWork(const QString& input,const QString& output,SourceProofPolicy policy,const QString& afterRun,const std::atomic_bool* cancel){
    SourceProofScan result;result.afterRun=afterRun;
    const auto cancelled=[&]{return cancel&&cancel->load(std::memory_order_relaxed);};
    try{
        if(cancelled()){result.cancelled=true;return result;}
        static const QRegularExpression runPattern("^[0-9a-f]{32}$");
        if(!afterRun.isEmpty()&&!runPattern.match(afterRun).hasMatch())fail("Invalid proof metadata scan key");
        const QFileInfo src(input),out(output);const auto source=src.canonicalFilePath();
        if(source.isEmpty()||src.isSymLink()||out.isSymLink()||source.compare(out.absoluteFilePath(),Qt::CaseInsensitive)==0||
            source.compare(out.canonicalFilePath(),Qt::CaseInsensitive)==0)fail("Proof scan requires distinct source/journal identities");
        std::unique_ptr<storage::ReadOnlyDatabase> journal;
        if(out.exists()){journal=std::make_unique<storage::ReadOnlyDatabase>(output);if(!validateJournal(journal->db))journal.reset();}
        storage::ReadOnlyDatabase db(source);
        sqlite3_progress_handler(db.db,1000,[](void* p)->int{return p&&static_cast<const std::atomic_bool*>(p)->load(std::memory_order_relaxed);},const_cast<std::atomic_bool*>(cancel));
        db.execute("BEGIN");
        Statement identities(db.db,"SELECT run,identity FROM native_source_identities WHERE run>? ORDER BY run LIMIT 65");identities.text(1,afterRun);
        int examined=0;const auto policyHash=policyIdentity(policy);
        while(identities.step()==SQLITE_ROW){
            if(cancelled()){result.cancelled=true;break;}
            if(examined++==64){result.more=true;break;}
            const auto run=identities.text(0);
            if(!runPattern.match(run).hasMatch()||sqlite3_column_type(identities.value,1)!=SQLITE_TEXT||
                sqlite3_column_bytes(identities.value,1)<1||sqlite3_column_bytes(identities.value,1)>16384)fail("Invalid native source metadata identity");
            result.afterRun=run;const auto identity=sha(identities.text(1).toUtf8());
            const auto progress=journal?cursor(journal->db,source,run,policyHash,identity):Cursor{};
            Statement last(db.db,"SELECT gap_id FROM origins WHERE run=? ORDER BY gap_id DESC LIMIT 1");last.text(1,run);
            qint64 through=0;if(last.step()==SQLITE_ROW)through=sqlite3_column_int64(last.value,0);
            if(through<progress.after)fail("Deferred native source watermark regressed");
            if(through>progress.after){result.run=run;result.more=true;break;}
        }
        db.execute("COMMIT");
    }catch(const std::exception& e){if(cancelled())result.cancelled=true;else result.error=QString::fromUtf8(e.what()).left(512);}
    return result;
}
} // namespace pgn::verification
