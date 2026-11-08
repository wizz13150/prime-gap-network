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
 require(view.value("phase")=="Searching"&&view.value("remaining").toString().startsWith("ETA ≈ "),"Search phase or estimated ETA changed scope");
 frame.insert("work_role","audit");frame.insert("title","Ticket 42 · Audit 1/2");view=render(frame);
 require(view.value("phase")=="Auditing partition"&&view.value("quantity").toString().startsWith("Current audit partition"),"Partition progress was presented as the whole audit");
 require(view.value("status").toString().contains("≈ 50.00%")&&rail.property("pgn_ticket_progress_fraction").toDouble()==.5,"Display altered measured progress");
 frame.insert("phase","Report ready");view=render(frame);
 require(view.value("phase")=="Audit partition computed"&&view.value("remaining").toString().isEmpty(),"Local computation implied network acceptance or kept an ETA");
 frame.insert("phase","Paused");view=render(frame);
 require(view.value("phase")=="Audit paused"&&!rail.property("pgn_ticket_animation_active").toBool(),"Paused audit still implies active calculation");
 QJsonObject preparing{{"phase","Starting"},{"audit_service",QJsonObject{{"active_role","audit"}}}};view=render(preparing);
 require(view.value("phase")=="Preparing audit"&&view.value("title")=="Network audit"&&!rail.property("pgn_ticket_progress_fraction").isValid(),"Preparation fabricated progress or appeared as unassigned search");
 view=render(QJsonObject{{"phase","Waiting"}});
 require(view.value("phase")=="Waiting"&&view.value("role").toString().isEmpty(),"Unknown work was guessed to be an audit");
 frame.remove("work_role");frame.insert("phase","Searching");frame.insert("title","Ticket 43");view=render(frame);
 require(view.value("phase")=="Searching"&&!view.value("quantity").toString().contains("audit"),"Audit state leaked into the next mining ticket");
 std::cout<<"PASS: seven real-widget work transitions; partition scope, estimates, no fabricated network verdict\n";
}
