#pragma once
#include <QFileInfo>
#include <QString>
#include <sqlite3.h>
#include <stdexcept>

namespace pgn::storage {
// A reader owns its connection on its worker thread. A missing archive never
// creates a new database; callers retain original strings and explicit errors.
class ReadOnlyDatabase {
public:
    sqlite3* db{};
    explicit ReadOnlyDatabase(const QString& file){
        if(!QFileInfo(file).isFile())throw std::runtime_error("Source database unavailable; no file created");
        const auto path=file.toUtf8();
        if(sqlite3_open_v2(path.constData(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){
            if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error("Read-only archive unavailable");
        }
        sqlite3_busy_timeout(db,1000);
    }
    ReadOnlyDatabase(const ReadOnlyDatabase&)=delete;
    ReadOnlyDatabase& operator=(const ReadOnlyDatabase&)=delete;
    ~ReadOnlyDatabase(){if(db)sqlite3_close(db);}
    void execute(const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
};
class ReadStatement {
public:
    sqlite3_stmt* value{};sqlite3* db{};
    ReadStatement(sqlite3* input,const char* sql):db(input){
        if(sqlite3_prepare_v2(db,sql,-1,&value,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));
    }
    ReadStatement(const ReadStatement&)=delete;
    ReadStatement& operator=(const ReadStatement&)=delete;
    ~ReadStatement(){if(value)sqlite3_finalize(value);}
    void integer(int index,qint64 input){if(sqlite3_bind_int64(value,index,input)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    void text(int index,const QString& input){const auto bytes=input.toUtf8();if(sqlite3_bind_text(value,index,bytes.constData(),bytes.size(),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    int step(){const auto result=sqlite3_step(value);if(result!=SQLITE_ROW&&result!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db));return result;}
    QString text(int column)const{const auto* bytes=sqlite3_column_text(value,column);return bytes?QString::fromUtf8(reinterpret_cast<const char*>(bytes)):QString{};}
};
}
