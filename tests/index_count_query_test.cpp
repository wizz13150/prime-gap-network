// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
// SQLite query-mechanism witness only; not a SourceHistory publication or GPU test.
#include <sqlite3.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks{}, failures{};
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
void need(int result, sqlite3* db) {
    if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
}
struct Result { sqlite3_int64 count; int vmSteps; double milliseconds; };
Result count(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement{};
    need(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr), db);
    const auto began = std::chrono::steady_clock::now();
    if (sqlite3_step(statement) != SQLITE_ROW) {
        sqlite3_finalize(statement); throw std::runtime_error("Count did not produce one row");
    }
    const auto value = sqlite3_column_int64(statement, 0);
    if (sqlite3_step(statement) != SQLITE_DONE) {
        sqlite3_finalize(statement); throw std::runtime_error("Count produced more than one row");
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    const auto steps = sqlite3_stmt_status(statement, SQLITE_STMTSTATUS_VM_STEP, 0);
    sqlite3_finalize(statement);
    return {value, steps, elapsed};
}
void print(const char* name, const Result& result) {
    std::cout << name << " rows=" << result.count << " vm_steps=" << result.vmSteps
              << " elapsed_ms=" << result.milliseconds << '\n';
}
void execute(sqlite3* db, const char* sql) { need(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), db); }
}

int main() {
    sqlite3* db{};
    try {
        need(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), db);
        execute(db, "CREATE TABLE gaps(id INTEGER PRIMARY KEY,unix_ts INTEGER); CREATE INDEX gaps_time ON gaps(unix_ts,id);");
        const auto emptyBefore = count(db, "SELECT COUNT(*) FROM gaps g WHERE 1");
        const auto emptyAfter = count(db, "SELECT COUNT(*) FROM gaps");
        check(emptyBefore.count == 0 && emptyAfter.count == 0, "empty database is genuinely zero");
        execute(db, "BEGIN; WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<100000) "
                    "INSERT INTO gaps SELECT i,CASE WHEN i%2=0 THEN 0 ELSE NULL END FROM n; COMMIT;");
        const auto before = count(db, "SELECT COUNT(*) FROM gaps g WHERE 1");
        const auto after = count(db, "SELECT COUNT(*) FROM gaps");
        const auto timeBefore = count(db, "SELECT COUNT(*) FROM gaps g WHERE 1 AND g.unix_ts IS NOT NULL");
        const auto timeAfter = count(db, "SELECT COUNT(*) FROM gaps g WHERE g.unix_ts IS NOT NULL");
        print("BASELINE_GLOBAL", before); print("CANDIDATE_GLOBAL", after);
        print("BASELINE_DATED", timeBefore); print("CANDIDATE_DATED", timeAfter);
        check(before.count == 100000 && after.count == before.count, "all identities preserved including undated");
        check(timeBefore.count == 50000 && timeAfter.count == timeBefore.count, "dated predicate preserves timestamp zero and excludes NULL only");
        check(before.vmSteps > 100000 && after.vmSteps < before.vmSteps / 100, "redundant predicate removal eliminates per-row VM loop");
        execute(db, "BEGIN; INSERT INTO gaps VALUES(100001,NULL); INSERT INTO gaps VALUES(100002,-1);");
        check(count(db, "SELECT COUNT(*) FROM gaps").count == 100002, "same transaction includes both new rows");
        check(count(db, "SELECT COUNT(*) FROM gaps g WHERE g.unix_ts IS NOT NULL").count == 50001, "negative timestamp is not invented or filtered");
        execute(db, "ROLLBACK;");
        check(count(db, "SELECT COUNT(*) FROM gaps").count == 100000, "rollback leaves original exact cardinality");
        execute(db, "BEGIN; DELETE FROM gaps WHERE id<=4; COMMIT;");
        check(count(db, "SELECT COUNT(*) FROM gaps").count == 99996, "delete reflected without cached count");
        check(count(db, "SELECT COUNT(*) FROM gaps g WHERE g.unix_ts IS NOT NULL").count == 49998, "dated delete reflected exactly");
        sqlite3_close(db); db = nullptr;
        std::cout << "RESULT checks=" << checks << " failures=" << failures
                  << " scope=SQLITE_QUERY_MECHANISM_NOT_FULL_HISTORY_SCALABILITY\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n'; if (db) sqlite3_close(db); return 2;
    }
}
