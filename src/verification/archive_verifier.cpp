#include "verification/archive_verifier.h"
#include "verification/bounded_gap_verifier.h"
#include "storage/sqlite_read.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVector>
#include <chrono>
#include <optional>

namespace pgn::verification {
namespace {
using Statement=storage::ReadStatement; // Shared prepared-statement wrapper; connection owns permissions.
constexpr int ApplicationId=0x50474e56;
struct SuppliedPair {qint64 id;QString lower,upper;};
struct CachedVerdict {GapVerdict verdict;};
class Journal {
public:
    sqlite3* db{};
    explicit Journal(const QString& file){
        if(QFileInfo(file).exists()){
            storage::ReadOnlyDatabase existing(file);
            Statement app(existing.db,"PRAGMA application_id"),version(existing.db,"PRAGMA user_version"),
                tables(existing.db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table'");
            app.step();version.step();tables.step();
            const auto id=sqlite3_column_int(app.value,0),schema=sqlite3_column_int(version.value,0);
            if(!((id==ApplicationId&&schema==1)||(id==0&&schema==0&&sqlite3_column_int(tables.value,0)==0)))
                throw std::runtime_error("Output is not an independent verification journal; left unchanged");
        }
        if(!QDir().mkpath(QFileInfo(file).absolutePath()))throw std::runtime_error("Verification journal directory unavailable");
        const auto path=file.toUtf8();
        if(sqlite3_open_v2(path.constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){
            if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error("Verification journal unavailable");
        }
        sqlite3_busy_timeout(db,1000);
        try{
            execute("PRAGMA journal_mode=WAL; BEGIN IMMEDIATE;");
            execute("CREATE TABLE IF NOT EXISTS gap_verifications("
                "id INTEGER PRIMARY KEY,source_id INTEGER NOT NULL,source_archive TEXT NOT NULL,"
                "p TEXT NOT NULL,q TEXT NOT NULL,method TEXT NOT NULL,strict_bound TEXT NOT NULL,"
                "validator_sha256 TEXT NOT NULL,odd_budget INTEGER NOT NULL,verdict INTEGER NOT NULL,"
                "checked_interior INTEGER NOT NULL,first_interior_prime TEXT NOT NULL,elapsed_ns INTEGER NOT NULL,"
                "checked_utc TEXT NOT NULL,network_credit INTEGER NOT NULL CHECK(network_credit=0),"
                "range_coverage INTEGER NOT NULL CHECK(range_coverage=0),"
                "UNIQUE(p,q,method,strict_bound,validator_sha256,odd_budget));"
                "CREATE TRIGGER IF NOT EXISTS gap_verification_no_update BEFORE UPDATE ON gap_verifications "
                "BEGIN SELECT RAISE(ABORT,'Independent verification evidence is append-only'); END;"
                "CREATE TRIGGER IF NOT EXISTS gap_verification_no_delete BEFORE DELETE ON gap_verifications "
                "BEGIN SELECT RAISE(ABORT,'Independent verification evidence is append-only'); END;"
                "PRAGMA application_id=1346850390; PRAGMA user_version=1; COMMIT;");
        }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);db=nullptr;throw;}
    }
    ~Journal(){if(db)sqlite3_close(db);}
    void execute(const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    std::optional<CachedVerdict> cached(const SuppliedPair& pair,const QString& validator,std::uint64_t budget){
        Statement query(db,"SELECT verdict,checked_interior,elapsed_ns,checked_utc FROM gap_verifications "
            "WHERE p=? AND q=? AND method=? AND strict_bound=? AND validator_sha256=? AND odd_budget=?");
        query.text(1,pair.lower);query.text(2,pair.upper);query.text(3,QString::fromLatin1(VerificationMethod.data(),VerificationMethod.size()));
        query.text(4,QString::fromLatin1(PrimeBound.data(),PrimeBound.size()));query.text(5,validator);query.integer(6,budget);
        if(query.step()!=SQLITE_ROW)return {};
        const auto verdict=sqlite3_column_int(query.value,0);
        const auto checks=sqlite3_column_int64(query.value,1);
        if(verdict<0||verdict>int(GapVerdict::BudgetExceeded)||checks<0||std::uint64_t(checks)>budget||
           sqlite3_column_int64(query.value,2)<0||!QDateTime::fromString(query.text(3),Qt::ISODateWithMs).isValid())
            throw std::runtime_error("Verification cache record invalid; no proof promotion");
        return CachedVerdict{static_cast<GapVerdict>(verdict)};
    }
    void append(const SuppliedPair& pair,const QString& source,const QString& validator,std::uint64_t budget,
                const GapResult& result,qint64 elapsed){
        execute("BEGIN IMMEDIATE");
        try{
            if(const auto old=cached(pair,validator,budget)){
                if(old->verdict!=result.verdict)throw std::runtime_error("Contradictory independent result for same supplied pair");
            }else{
                Statement insert(db,"INSERT INTO gap_verifications(source_id,source_archive,p,q,method,strict_bound,validator_sha256,"
                    "odd_budget,verdict,checked_interior,first_interior_prime,elapsed_ns,checked_utc,network_credit,range_coverage)"
                    "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,0,0)");
                insert.integer(1,pair.id);insert.text(2,source);insert.text(3,pair.lower);insert.text(4,pair.upper);
                insert.text(5,QString::fromLatin1(VerificationMethod.data(),VerificationMethod.size()));
                insert.text(6,QString::fromLatin1(PrimeBound.data(),PrimeBound.size()));insert.text(7,validator);
                insert.integer(8,budget);insert.integer(9,int(result.verdict));insert.integer(10,result.checkedInterior);
                insert.text(11,QString::fromStdString(result.firstInteriorPrime));insert.integer(12,elapsed);
                insert.text(13,QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));insert.step();
            }
            execute("COMMIT");
        }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
};
}
ArchiveVerificationBatch verifyArchivePage(const QString& input,const QString& output,const QString& validator,
    ArchiveVerificationPolicy policy,const std::atomic_bool* cancelled){
    ArchiveVerificationBatch batch;batch.after=policy.after;batch.through=policy.through;
    const auto interrupted=[&]{return cancelled&&cancelled->load(std::memory_order_relaxed);};
    try{
        static const QRegularExpression digest("^[0-9a-f]{64}$");
        if(policy.after<0||policy.through<0||policy.limit<1||policy.limit>64||policy.oddInteriorBudget>10000||!digest.match(validator).hasMatch())
            throw std::runtime_error("Invalid finite verification policy or validator identity");
        const auto source=QFileInfo(input).canonicalFilePath(),target=QFileInfo(output).absoluteFilePath();
        if(source.isEmpty()||source.compare(target,Qt::CaseInsensitive)==0||
           source.compare(QFileInfo(output).canonicalFilePath(),Qt::CaseInsensitive)==0)
            throw std::runtime_error("Verification source and journal must be separate existing identities");
        if(interrupted()){batch.cancelled=true;return batch;}
        QVector<SuppliedPair> pairs;
        {
            storage::ReadOnlyDatabase reader(source);reader.execute("BEGIN");
            Statement head(reader.db,"SELECT COALESCE(MAX(id),0) FROM gaps");head.step();
            const auto currentHead=sqlite3_column_int64(head.value,0);
            if(!batch.through)batch.through=currentHead;
            if(batch.through<policy.after||batch.through>currentHead)
                throw std::runtime_error("Source watermark invalid/regressed; explicit cursor recovery required");
            Statement query(reader.db,"SELECT id,p,q FROM gaps WHERE id>? AND id<=? ORDER BY id LIMIT ?");
            query.integer(1,policy.after);query.integer(2,batch.through);query.integer(3,policy.limit+1);
            while(query.step()==SQLITE_ROW){
                if(pairs.size()==policy.limit){batch.more=true;break;}
                if(sqlite3_column_type(query.value,1)!=SQLITE_TEXT||sqlite3_column_type(query.value,2)!=SQLITE_TEXT||
                   sqlite3_column_bytes(query.value,1)>160||sqlite3_column_bytes(query.value,2)>160)
                    throw std::runtime_error("Archive pair exceeds bounded transport contract; source untouched");
                pairs.append({sqlite3_column_int64(query.value,0),query.text(1),query.text(2)});
            }
            reader.execute("COMMIT");
        }
        if(pairs.isEmpty())return batch;
        Journal journal(target);
        for(const auto& pair:pairs){
            if(interrupted()){batch.cancelled=true;break;}
            GapVerdict verdict;
            if(const auto cached=journal.cached(pair,validator,policy.oddInteriorBudget)){
                verdict=cached->verdict;++batch.cached; // Original time/elapsed record is not refreshed.
            }else{
                const auto start=std::chrono::steady_clock::now();
                const auto result=verifyGap(pair.lower.toStdString(),pair.upper.toStdString(),policy.oddInteriorBudget,cancelled);
                const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                if(result.verdict==GapVerdict::Interrupted){batch.cancelled=true;break;}
                journal.append(pair,source,validator,policy.oddInteriorBudget,result,elapsed);
                verdict=result.verdict;++batch.evaluated;
            }
            if(verdict==GapVerdict::VerifiedWithinBound)++batch.verified;else ++batch.rejected;
            ++batch.processed;batch.after=pair.id;
        }
    }catch(const std::exception& error){batch.error=QString::fromUtf8(error.what());}
    return batch;
}
}
