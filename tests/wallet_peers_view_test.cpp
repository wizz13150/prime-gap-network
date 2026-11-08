#include "ui/wallet_peers_view.h"
#include "qt_offscreen_fixture.h"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static QJsonObject peer(qint64 id,bool inbound){return {{"id",id},{"addr",inbound?"[2001:db8::1]:48933":"192.0.2.5:48933"},{"subver","/PGN:1.0/"},{"inbound",inbound},{"conntime",qint64(1700000000)},{"bytesrecv",qint64(9007199254740993LL)},{"bytessent",qint64(512)},{"pingtime",.03215}};}
int main(int argc,char** argv){
 if(!pgn::test::prepareOffscreen())return 2;QApplication app(argc,argv);
 std::function<void(QJsonArray,QString)> reply;int reads=0;
 auto* view=new pgn::ui::WalletPeersView([&](auto done){++reads;reply=std::move(done);});
 require(reads==0,"Hidden peer panel started polling");view->refresh();view->refresh();require(reads==1,"Overlapping peer reads were admitted");
 reply({peer(10,true),peer(2,false)},{});auto* table=view->findChild<QTableWidget*>("pgn-wallet-peers");auto* status=view->findChild<QLabel*>("pgn-peers-status");
 require(table->rowCount()==2&&table->item(0,0)->text()=="2","Peer IDs were sorted lexically");
 require(table->item(0,6)->text()=="9007199254740993","Byte counters lost integer precision");
 require(table->item(0,1)->text()=="Outbound"&&table->item(1,1)->text()=="Inbound"&&table->item(0,5)->text().startsWith("≈ "),"Connection direction or latency estimate is wrong");
 table->setCurrentCell(1,0);view->present({peer(2,false),peer(10,true)});require(table->item(table->currentRow(),0)->text()=="10","Refresh lost the selected peer identity");
 view->refresh();reply({},"RPC disconnected");require(table->rowCount()==2&&view->property("pgn_peers_stale").toBool()&&status->text().contains("previous snapshot"),"Read failure erased known peers or claimed freshness");
 view->present({peer(2,false),peer(2,true)});require(table->rowCount()==2&&view->property("pgn_peers_stale").toBool(),"Duplicate peer identities were accepted");
 auto invalid=peer(3,true);invalid.insert("bytessent",-1);view->present({invalid});require(table->rowCount()==2&&view->property("pgn_peers_stale").toBool(),"Invalid byte count replaced the snapshot");
 auto noPing=peer(3,false);noPing.remove("pingtime");view->present({noPing});require(table->rowCount()==1&&table->item(0,5)->text()=="—"&&!view->property("pgn_peers_stale").toBool(),"Missing latency was fabricated or stale state was retained");
 view->present({});require(table->rowCount()==0&&view->property("pgn_peer_count").toInt()==0&&status->text().startsWith("0 connected peers"),"A valid empty snapshot was confused with an error");
 if(argc==3){
  QFile file(QString::fromLocal8Bit(argv[1]));require(file.open(QIODevice::ReadOnly)&&file.size()<=262144,"Invalid bounded actual-peer fixture");QJsonParseError error;const auto actual=QJsonDocument::fromJson(file.readAll(),&error);require(error.error==QJsonParseError::NoError&&actual.isArray(),"Actual peer RPC did not return an array");
  view->present(actual.array());require(!view->property("pgn_peers_stale").toBool()&&table->rowCount()==actual.array().size(),"Actual Core peers were rejected by the display");view->resize(1366,440);QImage image(view->size(),QImage::Format_ARGB32);image.fill(Qt::transparent);view->render(&image);require(image.save(QString::fromLocal8Bit(argv[2])),"Actual peer rendering was not saved");
 }
 view->refresh();delete view;reply({peer(9,false)},{});
 std::cout<<"PASS: peer sorting, exact byte counters, direction, selection, stale/error recovery, empty state, no overlapping reads, safe late reply\n";
}
