#include "verification/journal_reader.h"
#include <QFileInfo>
#include <sqlite3.h>
#include <memory>
#include <stdexcept>

namespace pgn::verification {
namespace {
using Database=std::unique_ptr<sqlite3,decltype(&sqlite3_close)>;
using Statement=std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3* db,const char* sql){
    sqlite3_stmt* statement{};
    if(sqlite3_prepare_v2(db,sql,-1,&statement,nullptr)!=SQLITE_OK)
        throw std::runtime_error("Verification journal query unavailable");
    return Statement(statement,sqlite3_finalize);
}
int scalar(sqlite3* db,const char* sql){
    auto statement=prepare(db,sql);
    if(sqlite3_step(statement.get())!=SQLITE_ROW)throw std::runtime_error("Verification journal identity unavailable");
    return sqlite3_column_int(statement.get(),0);
}
QString text(sqlite3_stmt* row,int column,int maximum){
    const int bytes=sqlite3_column_bytes(row,column);
    if(sqlite3_column_type(row,column)!=SQLITE_TEXT||bytes<1||bytes>maximum)
        throw std::runtime_error("Malformed verification receipt text");
    return QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(row,column)),bytes);
}
qint64 integer(sqlite3_stmt* row,int column){
    if(sqlite3_column_type(row,column)!=SQLITE_INTEGER)
        throw std::runtime_error("Malformed verification receipt integer");
    return sqlite3_column_int64(row,column);
}
bool less(const QString& a,const QString& b){return a.size()<b.size()||(a.size()==b.size()&&a<b);}
bool decimal(const QString& value){
    // A receipt decoder, not a mathematical verifier: ASCII/canonical/schema
    // bounds only. Keep GMP and all proof computation outside the GUI process.
    if(value.isEmpty()||value.size()>int(PrimeBound.size())||(value.size()>1&&value.front()=='0'))return false;
    for(const QChar digit:value)if(digit<'0'||digit>'9')return false;
    return true;
}
// Decimal subtraction avoids narrowing endpoint integers into qint64. The
// receipt's resource budget bounds the difference used for positive checking.
QString subtract(QString larger,const QString& smaller){
    int borrow=0,j=smaller.size()-1;
    for(int i=larger.size()-1;i>=0;--i,--j){
        int digit=larger[i].digitValue()-borrow-(j>=0?smaller[j].digitValue():0);
        borrow=digit<0;larger[i]=QChar('0'+digit+(borrow?10:0));
    }
    int leading=0;while(leading+1<larger.size()&&larger[leading]=='0')++leading;
    return larger.mid(leading);
}
}
LatestReceipt readLatestReceipt(const QString& file,const QStringList& qualifiedValidators){
    LatestReceipt result;
    try {
        const QFileInfo info(file); // Stat before reading a growing evidence journal.
        if(!info.isFile()||info.size()==0)throw std::runtime_error("Independent verification journal absent");
        sqlite3* raw{};
        const auto filename=info.absoluteFilePath().toUtf8();
        const auto opened=sqlite3_open_v2(filename.constData(),&raw,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr);
        int querySteps=0; // Outlives the SQLite handle and its progress callback.
        Database db(raw,sqlite3_close);
        if(opened!=SQLITE_OK)throw std::runtime_error("Independent verification journal unavailable");
        sqlite3_limit(db.get(),SQLITE_LIMIT_LENGTH,16384);
        sqlite3_limit(db.get(),SQLITE_LIMIT_SQL_LENGTH,2048);
        sqlite3_progress_handler(db.get(),1000,[](void* counter)->int{
            return ++*static_cast<int*>(counter)>100;
        },&querySteps);
        sqlite3_busy_timeout(db.get(),50);
        if(scalar(db.get(),"PRAGMA application_id")!=1346850390||scalar(db.get(),"PRAGMA user_version")!=1)
            throw std::runtime_error("Not a qualified independent verification journal");
        auto statement=prepare(db.get(),"SELECT id,source_id,source_archive,p,q,method,strict_bound,validator_sha256,odd_budget,verdict,checked_interior,elapsed_ns,checked_utc,network_credit,range_coverage FROM gap_verifications ORDER BY id DESC LIMIT 1");
        const int step=sqlite3_step(statement.get());
        if(step==SQLITE_DONE)return result; // Empty != a zero-rate proof observation.
        if(step!=SQLITE_ROW)throw std::runtime_error("Latest verification receipt unavailable");
        auto* row=statement.get();JournalReceipt receipt;
        receipt.id=integer(row,0);receipt.sourceId=integer(row,1);
        receipt.sourceArchive=text(row,2,4096);receipt.lower=text(row,3,24);receipt.upper=text(row,4,24);
        const auto method=text(row,5,64),bound=text(row,6,24);
        receipt.validatorSha256=text(row,7,64);
        receipt.oddBudget=integer(row,8);const auto verdict=integer(row,9);
        receipt.checkedInterior=integer(row,10);receipt.elapsedNs=integer(row,11);
        const auto checked=text(row,12,40);
        receipt.originalCheckedUtc=QDateTime::fromString(checked,Qt::ISODateWithMs);
        if(receipt.id<=0||receipt.sourceId<=0||receipt.oddBudget<0||receipt.oddBudget>10000||
           verdict<0||verdict>qint64(GapVerdict::BudgetExceeded)||receipt.checkedInterior<0||
           receipt.checkedInterior>receipt.oddBudget||receipt.elapsedNs<0||
           !receipt.originalCheckedUtc.isValid()||!checked.endsWith('Z')||
           method!=QString::fromLatin1(VerificationMethod.data(),VerificationMethod.size())||
           bound!=QString::fromLatin1(PrimeBound.data(),PrimeBound.size())||
           receipt.validatorSha256.size()!=64||
           !qualifiedValidators.contains(receipt.validatorSha256,Qt::CaseInsensitive)||
           !decimal(receipt.lower)||!decimal(receipt.upper)||!less(receipt.lower,receipt.upper)||
           integer(row,13)!=0||integer(row,14)!=0)
            throw std::runtime_error("Unqualified or inconsistent verification receipt");
        receipt.verdict=GapVerdict(verdict);
        if(receipt.verdict==GapVerdict::VerifiedWithinBound){
            if(!less(receipt.upper,bound))throw std::runtime_error("Positive receipt outside strict proof domain");
            bool fits=false;const auto distance=subtract(receipt.upper,receipt.lower).toULongLong(&fits);
            const bool special=receipt.lower=="2"&&receipt.upper=="3";
            const bool odd=receipt.lower.back().digitValue()%2==1&&receipt.upper.back().digitValue()%2==1&&less("2",receipt.lower);
            if(!fits||(!special&&!odd)||distance>quint64(receipt.oddBudget)*2+2||
               receipt.checkedInterior!=qint64(special?0:distance/2-1))
                throw std::runtime_error("Positive receipt lacks coherent complete interior checking");
            receipt.verifiedGapSize=distance;
        }
        result.receipt=std::move(receipt);
    }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
    return result;
}
}
