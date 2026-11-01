#include "booking/booking_store.h"
#include "storage/sqlite_read.h"
#include <QRegularExpression>

namespace pgn::booking {
BookingEventPage readBookingEvents(const QString& file,qint64 before,qint64 through,const QString& bookingId,int limit){
    BookingEventPage result;
    try{
        static const QRegularExpression identity("^[0-9a-f]{32}$|^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$");
        if(before<0||through<0||limit<1||limit>256||(!bookingId.isEmpty()&&!identity.match(bookingId).hasMatch()))
            throw std::runtime_error("Invalid bounded lifecycle query");
        storage::ReadOnlyDatabase connection(file);connection.execute("BEGIN");
        if(!through){storage::ReadStatement head(connection.db,"SELECT COALESCE(MAX(sequence),0) FROM booking_events");head.step();through=sqlite3_column_int64(head.value,0);}
        result.through=through;
        if(!before){storage::ReadStatement count(connection.db,bookingId.isEmpty()?
            "SELECT COUNT(*) FROM booking_events WHERE sequence<=?":
            "SELECT COUNT(*) FROM booking_events WHERE sequence<=? AND booking_id=?");
            count.integer(1,through);if(!bookingId.isEmpty())count.text(2,bookingId);count.step();result.total=sqlite3_column_int64(count.value,0);}
        storage::ReadStatement rows(connection.db,bookingId.isEmpty()?
            "SELECT sequence,booking_id,start,end,device,kind,from_state,to_state,event_kind,observed_utc FROM booking_events WHERE sequence<=? AND (?=0 OR sequence<?) ORDER BY sequence DESC LIMIT ?":
            "SELECT sequence,booking_id,start,end,device,kind,from_state,to_state,event_kind,observed_utc FROM booking_events WHERE sequence<=? AND (?=0 OR sequence<?) AND booking_id=? ORDER BY sequence DESC LIMIT ?");
        rows.integer(1,through);rows.integer(2,before);rows.integer(3,before);
        if(!bookingId.isEmpty())rows.text(4,bookingId);rows.integer(bookingId.isEmpty()?4:5,limit+1);
        while(rows.step()==SQLITE_ROW){if(result.rows.size()==limit){result.more=true;break;}
            result.rows.append({sqlite3_column_int64(rows.value,0),rows.text(1),rows.text(2),rows.text(3),rows.text(4),rows.text(5),rows.text(6),rows.text(7),rows.text(8),rows.text(9)});}
        connection.execute("COMMIT");
    }catch(const std::exception& e){result.rows.clear();result.error=QString::fromUtf8(e.what());}
    return result;
}
}
