#include "ui/network_work_view.h"
#include "wallet/public_work_snapshot.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){QApplication app(argc,argv);if(argc!=3)throw std::runtime_error("Expected actual Core snapshot and pinned SHA256");int checks=0;
 const auto need=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
 QFile file(QString::fromLocal8Bit(argv[1]));need(file.open(QIODevice::ReadOnly)&&file.size()<=524288,"bounded snapshot file");const auto bytes=file.readAll();
 need(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==argv[2],"actual Core snapshot hash");
 QJsonParseError parse;const auto snapshot=QJsonDocument::fromJson(bytes,&parse).object();need(parse.error==QJsonParseError::NoError,"Core snapshot JSON");
 const auto state=snapshot.value("state").toObject();const auto request=snapshot.value("request_id").toString();
 need(pgn::wallet::validatePublicWorkSnapshot(snapshot,state.value("genesis").toString(),"tickets",0,1,request).isEmpty(),"coherent actual Core snapshot");
 auto page=state;page.insert("reports",snapshot.value("reports"));page.insert("audits",snapshot.value("audits"));
 const auto expected=QString::fromUtf8("Émile-31");need(pgn::ui::publicReportName(page)==expected,"real disclosed label binding");
 int fallback=0;pgn::ui::NetworkWorkView view([&](int,pgn::ui::NetworkWorkView::Result done){done(page,{});},[&](const QString&,pgn::ui::NetworkWorkView::Result done){++fallback;done({},"Unexpected fallback");});
 view.resize(1280,800);view.show();for(int i=0;i<10;++i)app.processEvents();
 auto* table=view.findChild<QTableWidget*>("pgn-network-ticket-table");auto* evidence=view.findChild<QTreeWidget*>();need(table&&evidence&&table->rowCount()==1,"actual one-ticket widget row");
 const auto owner=page.value("leases").toArray().first().toObject().value("miner_script").toString();need(!owner.isEmpty(),"actual owner signing identity present");
 bool nameShown=false,ownerShown=false;for(int i=0;i<evidence->topLevelItemCount();++i){const auto* item=evidence->topLevelItem(i);if(item->text(0)=="Public discoverer name")nameShown=item->text(1)==expected;if(item->text(0)=="Owner signing script")ownerShown=item->text(1)==owner;}
 need(nameShown&&ownerShown,"actual Unicode name shown beside exact owner script");need(fallback==0,"no extra RPC needed for complete actual snapshot");
 need(!page.value("reports").toArray().first().toObject().value("scientific_accepted").toBool(),"synthetic fixture confers no scientific acceptance");
 std::cout<<"PASS "<<checks<<" actual private Core snapshot -> validated public projection -> real offscreen ticket/name/owner widgets; synthetic counts, one human operator\n";
}
