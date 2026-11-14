#include "ui/mining_ticket_progress.h"
#include "qt_offscreen_fixture.h"
#include <QApplication>
#include <QImage>
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
 if(!pgn::test::prepareOffscreen())return 2;
 QApplication app(argc,argv);
 pgn::ui::MiningTicketProgress rail;rail.resize(1280,148);
 const auto render=[&](QJsonObject frame){
  rail.present(frame);QImage image(rail.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);rail.render(&image);
  return rail.property("pgn_ticket_work_presentation").toJsonObject();
 };
 QJsonObject frame{{"phase","Searching"},{"title","Ticket 42"},{"bits",68},{"positions","1000000000000"},{"positions_total","2000000000000"},{"eta_rate",10000000000.}};
 auto view=render(frame);
 require(rail.height()==120&&rail.property("pgn_ticket_header_rows").toInt()==1,"Compact header no longer frees the second metadata row");
 require(view.value("phase")=="Searching"&&view.value("remaining").toString().startsWith("ETA ≈ "),"Search phase or estimated ETA changed scope");
 frame.insert("work_role","audit");frame.insert("title","Ticket 42 · Audit 1/2");view=render(frame);
 require(view.value("phase")=="Auditing"&&view.value("quantity").toString().startsWith("Complete audit"),"Audit quantity is not identified as a whole audit");
 require(view.value("status").toString().contains("≈ 50.00%")&&rail.property("pgn_ticket_progress_fraction").toDouble()==.5,"Display altered measured progress");
 frame.insert("phase","Report ready");view=render(frame);
 require(view.value("phase")=="Audit computed"&&view.value("remaining").toString().isEmpty(),"Local computation implied network acceptance or kept an ETA");
 frame.insert("phase","Paused");view=render(frame);
 require(view.value("phase")=="Audit paused"&&!rail.property("pgn_ticket_animation_active").toBool(),"Paused audit still implies active calculation");
 QJsonObject preparing{{"phase","Starting"},{"audit_service",QJsonObject{{"active_role","audit"}}}};view=render(preparing);
 require(view.value("phase")=="Preparing audit"&&view.value("title")=="Network audit"&&!rail.property("pgn_ticket_progress_fraction").isValid(),"Preparation fabricated progress or appeared as unassigned search");
 view=render(QJsonObject{{"phase","Waiting"}});
 require(view.value("phase")=="Waiting"&&view.value("role").toString().isEmpty(),"Unknown work was guessed to be an audit");
 frame.remove("work_role");frame.insert("phase","Searching");frame.insert("title","Ticket 43");view=render(frame);
 require(view.value("phase")=="Searching"&&!view.value("quantity").toString().contains("audit"),"Audit state leaked into the next mining ticket");
 // Exercise the real widget: an error at 100% is neither success nor a known
 // recovery. Its unelided reason must stay reachable, then clear on fresh work.
 const auto reason=QString("storage WAL budget <4096>\nCause: busy reader ")+QString(420,QChar('x'));
 frame.insert("phase","Needs attention");frame.insert("positions",frame.value("positions_total"));frame.insert("message",reason);view=render(frame);
 require(view.value("status").toString().contains("storage unavailable")&&!view.value("status").toString().contains("100%")&&view.value("remaining").toString().isEmpty(),"A failed completed computation showed success or a completion ETA");
 require(rail.toolTip().contains("&lt;4096&gt;")&&rail.toolTip().contains(QString(420,QChar('x')))&&!rail.toolTip().contains("<4096>"),"Full cause was lost or interpreted as markup");
 require(rail.accessibleDescription().contains("storage unavailable"),"Accessible failure explanation missing");
 frame.insert("phase","Searching");frame.insert("positions","1000000000000");view=render(frame);
 require(rail.toolTip().isEmpty()&&rail.accessibleDescription().isEmpty()&&view.value("phase")=="Searching", "Historical failure leaked into recovered current work");
 std::cout<<"PASS: real-widget phases, truthful failure text/full cause/accessibility, recovery clears stale explanation\n";
}
