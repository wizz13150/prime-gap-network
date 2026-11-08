#include "ui/network_work_view.h"
#include "ui/audit_animation_view.h"
#include "ui/audit3d/audit_native_surface.h"
#include "ui/panel_style.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QJsonDocument>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc!=4)return 2;app.setStyleSheet(pgn::ui::dataPanelStyle());
 const auto load=[](const char* path){QFile f(QString::fromLocal8Bit(path));if(!f.open(QIODevice::ReadOnly))return QJsonObject{};return QJsonDocument::fromJson(f.readAll()).object();};
 const auto input=load(argv[1]),global=load(argv[2]);if(input.isEmpty()||global.isEmpty())return 3;const auto page=input["page"].toObject(),atlas=input["atlas"].toObject();
 pgn::ui::NetworkWorkView view([page](int,auto done){done(page,{});},[page,input](const QString& id,auto done){
  if(input["details"].toObject().contains(id)){done(input["details"].toObject()[id].toObject(),{});return;}
  auto value=page;for(const auto* key:{"leases","reports","audits"}){QJsonArray rows;for(const auto& row:page[key].toArray())if(row.toObject()[key==QString("leases")?"request":"request_id"].toString()==id)rows.append(row);value[key]=rows;}done(value,{});
 },nullptr,[atlas,input](const QString& start,const QString& end,int,auto done){
  const auto active=input["active_atlas"].toObject();if(start.isEmpty())done(atlas,{});else if(start==active["start"].toString()&&end==active["end"].toString())done(active,{});else done({},"Fixture viewport not captured");
 });
 auto* animation=new pgn::ui::AuditAnimationView([global](qint64 height,const QString&,const QJsonArray&,auto done){auto value=global;value["after_height"]=height;value["rebase"]=height==0;done(value,{});},[&view](const QJsonArray& rows,QWidget* surface){return view.auditProjection(rows,surface);});
 view.onAtlasProjectionChanged=[animation]{animation->reproject();};view.setAuditAnimation(animation);view.resize(1366,960);view.move(-30000,-30000);view.show();
 bool done=false,started=false;QElapsedTimer elapsed;elapsed.start();QTimer poll;poll.setInterval(50);int beats=0;QTimer heartbeat;heartbeat.setInterval(10);QObject::connect(&heartbeat,&QTimer::timeout,[&]{++beats;});heartbeat.start();
 QObject::connect(&poll,&QTimer::timeout,[&]{pgn::audit3d::Surface* surface=nullptr;for(auto* child:animation->findChildren<QWidget*>())if(auto* candidate=dynamic_cast<pgn::audit3d::Surface*>(child))surface=candidate;if(!surface)return;
  const auto error=animation->property("pgn_audit_animation_error").toString();if(!error.isEmpty()){QJsonObject result{{"PASS",false},{"error",error},{"resource_requests",surface->property("pgn_audit3d_resource_requests").toInt()},{"last_resource",surface->property("pgn_audit3d_last_resource").toString()},{"resource_error",surface->property("pgn_audit3d_resource_error").toString()},{"blocked",surface->property("pgn_audit3d_external_requests_blocked").toInt()}};QFile f(QString::fromLocal8Bit(argv[3])+".json");if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(result).toJson());app.exit(4);return;}
  if(surface->property("pgn_audit3d_ready").toBool()&&!started){started=true;animation->preview();QTimer::singleShot(12000,&view,[&,surface]{surface->capture(QString::fromLocal8Bit(argv[3])+".surface.png",[&,surface](bool saved){if(!saved){app.exit(5);return;}auto image=view.grab().toImage();QPainter painter(&image);painter.drawImage(QRect(surface->mapTo(&view,QPoint()),surface->size()),QImage(QString::fromLocal8Bit(argv[3])+".surface.png"));painter.end();done=image.save(QString::fromLocal8Bit(argv[3])+".png");QJsonObject result{{"PASS",done},{"native_panel",true},{"preview_is_simulated",true},{"live_network_mutations",false},{"heartbeat_count",beats},{"elapsed_ms",elapsed.elapsed()},{"table_rows",view.findChild<QTableWidget*>()->rowCount()},{"animation_height",animation->height()},{"table_height",view.findChild<QTableWidget*>()->height()}};QFile file(QString::fromLocal8Bit(argv[3])+".json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(result).toJson());app.quit();});});}});
 poll.start();QTimer::singleShot(35000,&app,[&]{app.exit(4);});app.exec();return done?0:4;
}
