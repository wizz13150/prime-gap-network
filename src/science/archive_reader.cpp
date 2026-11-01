#include "science/archive_reader.h"
#include "storage/sqlite_read.h"
#include <QRegularExpression>
namespace pgn::science {
namespace {
using Database=storage::ReadOnlyDatabase;
using Statement=storage::ReadStatement;
}
ObservationPage readObservations(const QString& file,qint64 after,qint64 through,const QString& left,int limit){
 ObservationPage result;
 try{
  static const QRegularExpression decimal("^(0|[1-9][0-9]{0,159})$");
  if(after<0||through<0||limit<1||limit>256||(!left.isEmpty()&&!decimal.match(left).hasMatch()))throw std::runtime_error("Invalid bounded observation query");
  Database connection(file);connection.execute("BEGIN");
  const bool capturedHead=!through;
  if(capturedHead){Statement head(connection.db,"SELECT COALESCE(MAX(id),0) FROM gaps");head.step();through=sqlite3_column_int64(head.value,0);}
  result.through=through;
  if(!after){
   // MAX(id) and this count share one read transaction: every visible row is
   // already <= that freshly captured maximum, including databases with holes.
   // Removing the redundant predicate preserves the exact snapshot and lets
   // SQLite count its compact index instead of reading the entire gap table.
   const bool allAtHead=capturedHead&&left.isEmpty();
   Statement count(connection.db,allAtHead?"SELECT COUNT(*) FROM gaps":left.isEmpty()?"SELECT COUNT(*) FROM gaps WHERE id<=?":"SELECT COUNT(*) FROM gaps WHERE id<=? AND p=?");
   if(!allAtHead)count.integer(1,through);if(!left.isEmpty())count.text(2,left);count.step();result.total=sqlite3_column_int64(count.value,0);}
  Statement rows(connection.db,left.isEmpty()?
   "SELECT id,p,q,gap,merit,unix_ts FROM gaps WHERE id>? AND id<=? ORDER BY id LIMIT ?":
   "SELECT id,p,q,gap,merit,unix_ts FROM gaps WHERE id>? AND id<=? AND p=? ORDER BY id LIMIT ?");
  rows.integer(1,after);rows.integer(2,through);if(!left.isEmpty())rows.text(3,left);rows.integer(left.isEmpty()?3:4,limit+1);
  while(rows.step()==SQLITE_ROW){
   if(result.rows.size()==limit){result.more=true;break;}
   result.rows.append({sqlite3_column_int64(rows.value,0),rows.text(1),rows.text(2),rows.text(3),rows.text(4),rows.text(5)});
  }
  connection.execute("COMMIT");
 }catch(const std::exception& e){result.rows.clear();result.error=QString::fromUtf8(e.what());}
 return result;
}
OriginPage readOrigins(const QString& file,qint64 gapId,const QString& afterRun,const QString& afterSource,int limit){
 OriginPage result;
 try{
  if(gapId<1||limit<1||limit>256||afterRun.size()>128||afterSource.size()>4096)throw std::runtime_error("Invalid bounded source-origin query");
  Database connection(file);
  Statement rows(connection.db,"SELECT run,source FROM origins WHERE gap_id=? AND (run>? OR (run=? AND source>?)) ORDER BY run,source LIMIT ?");
  rows.integer(1,gapId);rows.text(2,afterRun);rows.text(3,afterRun);rows.text(4,afterSource);rows.integer(5,limit+1);
  while(rows.step()==SQLITE_ROW){if(result.rows.size()==limit){result.more=true;break;}result.rows.append({rows.text(0),rows.text(1)});}
 }catch(const std::exception& e){result.rows.clear();result.error=QString::fromUtf8(e.what());}
 return result;
}
}
