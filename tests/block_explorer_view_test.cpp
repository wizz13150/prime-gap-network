#include "ui/block_explorer_view.h"
#include <QImage>
#include <QDir>
#include <QSplitter>
#include <QSet>
#include <iostream>
#include <stdexcept>
using namespace pgn::ui;
static void require(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
static QJsonObject rawBlock(int height,const QString& hash){return {{"hash",hash},{"height",height},{"confirmations",2},{"time",1700000000+height},{"proof_type","coordination"},{"previousblockhash",QString(64,'d')},{"nextblockhash",QString(64,'e')},{"tx",QJsonArray{QJsonObject{{"txid",QString(64,'f')},{"size",120},{"weight",480},{"vout",QJsonArray{QJsonObject{{"n",0},{"value",0},{"scriptPubKey",QJsonObject{{"asm","OP_RETURN 50474e2d5245504f5254"},{"hex","6a"}}}}}}}}}};}
static QJsonObject block(int height,const QString& hash){
 const auto value=rawBlock(height,hash);const QJsonObject envelope{{"id","currency-fixture"},{"result",value}};
 const auto exact=pgn::wallet::annotateExactRpcMoney(QJsonDocument(envelope).toJson(QJsonDocument::Compact),"currency-fixture","getblock",value);
 require(exact.error.isEmpty(),"Exact monetary fixture annotation failed");return exact.value.toObject();
}
static QJsonObject txFixture(const QString& id,const QString& hash){
 auto tx=rawBlock(100,hash).value("tx").toArray().first().toObject();tx["txid"]=id;tx["blockhash"]=hash;tx["confirmations"]=2;tx["vsize"]=120;tx["version"]=2;tx["locktime"]=0;tx["vin"]=QJsonArray{QJsonObject{{"txid",QString(64,'c')},{"vout",1}}};
 const auto exact=pgn::wallet::annotateExactRpcMoney(QJsonDocument(QJsonObject{{"id","tx-fixture"},{"result",tx}}).toJson(QJsonDocument::Compact),"tx-fixture","getrawtransaction",tx);
 require(exact.error.isEmpty(),"Transaction fixture lost exact money");return exact.value.toObject();
}
static QJsonObject page(int height,const QString& hash){auto row=block(height,hash);row["nTx"]=1;return {{"height",height},{"tip_height",height},{"bestblockhash",hash},{"gold_blocks",0},{"minted_atomic","1710000000000"},{"observed_utc","fixture"},{"blocks",QJsonArray{row}}};}
static QJsonObject projectionBlock(const QString& hash,int offset){
 auto value=block(100,hash);QJsonArray txs;
 for(int i=offset;i<std::min(offset+25,51);++i)txs.append(QJsonObject{{"txid",QString::number(i+1,16).rightJustified(64,'0')},{"size",500000},{"weight",2000000},{"vin_total",2},{"vout_total",53},{"output_atomic","0"}});
 value["schema"]="pgn-explorer-view-1";value["kind"]="block";value["tx_total"]=51;value["nTx"]=51;value["tx_offset"]=offset;value["tx_limit"]=25;value["coinbase_output_atomic"]="0";value["tx"]=txs;return value;
}
static QJsonObject projectionTx(const QString& id,const QString& hash,int offset){
 auto value=txFixture(id,hash);QJsonArray inputs,outputs;
 for(int n=offset;n<std::min(offset+25,2);++n)inputs.append(QJsonObject{{"n",n},{"txid",QString(64,'c')},{"vout",n}});
 for(int n=offset;n<std::min(offset+25,53);++n)outputs.append(QJsonObject{{"n",n},{"value",0},{"scriptPubKey",QJsonObject{{"bytes",500000},{"hex_preview",QString(192,'0')},{"truncated",true}}}});
 value["schema"]="pgn-explorer-view-1";value["kind"]="transaction";value["vin_total"]=2;value["vout_total"]=53;value["io_offset"]=offset;value["io_limit"]=25;value["output_atomic"]="0";value["vin"]=inputs;value["vout"]=outputs;
 // Remove the earlier fixture's exact sidecars, then annotate the real page envelope.
 value.remove("_pgn_atomic");const auto decoded=pgn::wallet::annotateExactRpcMoney(QJsonDocument(QJsonObject{{"id","page-fixture"},{"result",value}}).toJson(QJsonDocument::Compact),"page-fixture","getpgnexplorerview",value);
 require(decoded.error.isEmpty(),"Projection page exact annotation failed");return decoded.value.toObject();
}
int main(int argc,char** argv){QApplication app(argc,argv);QFont font("Segoe UI");font.setPixelSize(13);app.setFont(font);QPalette palette;palette.setColor(QPalette::Window,QColor("#020e14"));palette.setColor(QPalette::WindowText,QColor("#d9f3f4"));palette.setColor(QPalette::Base,QColor("#051b23"));palette.setColor(QPalette::Text,QColor("#d9f3f4"));palette.setColor(QPalette::PlaceholderText,QColor("#88bbc6"));palette.setColor(QPalette::Button,QColor("#0d323a"));palette.setColor(QPalette::ButtonText,QColor("#d9f3f4"));palette.setColor(QPalette::Highlight,QColor("#245f68"));palette.setColor(QPalette::HighlightedText,Qt::white);app.setPalette(palette);std::vector<BlockExplorerServices::Reply> details,pages,txReplies;std::vector<int> heights;QStringList txids,txBlocks;
 BlockExplorerServices services{[&](QString,auto done){details.push_back(std::move(done));},[&](int height,auto done){heights.push_back(height);details.push_back(std::move(done));},[&](int height,auto done){heights.push_back(height);pages.push_back(std::move(done));},[&](QString id,QString hash,auto done){txids.push_back(id);txBlocks.push_back(hash);txReplies.push_back(std::move(done));}};
 auto* view=new BlockExplorerView(services);view->resize(1380,900);view->show();app.processEvents();const QString a(64,'a'),b(64,'b');view->presentSnapshot(page(100,a));require(details.size()==1,"First verified page did not open a block");details.back()(block(100,a),{});
 view->inspectHash(a);auto old=details.back();view->inspectHash(b);auto latest=details.back();latest(block(101,b),{});old(block(100,a),{});require(view->property("pgn_explorer_selected_block").toJsonObject().value("hash")==b,"Late block request overwrote newer user selection");
 view->presentSnapshot(page(101,b));auto* table=view->findChild<QTableWidget*>("pgn-native-network-blocks");require(table&&table->rowCount()==1&&table->selectionModel()->selectedRows().size()==1,"Selected block was not visibly retained");auto* same=table->item(0,0);for(int i=0;i<25;++i)view->presentSnapshot(page(101,b));require(table->item(0,0)==same&&table->selectionModel()->selectedRows().size()==1,"Unchanged refresh rebuilt cells or lost selection");
 require(view->findChild<QLabel*>("pgn-explorer-issued")->text()=="171 PGN","Issued amount formatted incorrectly");require(explorerAtomicAmount("999999999999999999")=="99999999.9999999999 PGN","Large integer amount lost precision");require(explorerAtomicAmount("1e20")=="—","Noncanonical amount accepted");require(explorerAtomicAmount("1000000000000000001")=="—","Above-supply amount accepted");
 require(explorerAtomicAmount("999999999999999999","tPGN")=="99999999.9999999999 tPGN","Testnet large amount changed its exact value");
 for(const auto& chain:QStringList{"main","test","regtest","unknown"}){
  const QString unit=chain=="main"?"PGN":chain=="unknown"?QString{}:"tPGN";
  BlockExplorerServices immediate{[&](QString hash,auto done){done(block(100,hash),{});},[&](int height,auto done){done(block(height,a),{});},[&](int height,auto done){done(page(height,a),{});}};
  BlockExplorerView network(immediate,nullptr,chain);network.resize(1100,760);network.presentSnapshot(page(100,a));app.processEvents();
  require(network.findChild<QLabel*>("pgn-explorer-issued")->text()==(unit.isEmpty()?QString("—"):QString("171 ")+unit),"Explorer issued denomination differs from its network");
  const auto overview=network.findChild<QTreeWidget*>("pgn-explorer-overview")->findItems("Coinbase outputs",Qt::MatchExactly);
  require(overview.size()==1&&overview.first()->text(1)==(unit.isEmpty()?QString("—"):QString("0.0000000000 ")+unit),"Coinbase output has wrong denomination");
  auto* tx=network.findChild<QTableWidget*>("pgn-explorer-transactions");
  require(tx->horizontalHeaderItem(3)->text()=="Output · "+(unit.isEmpty()?QString("—"):unit),"Transaction output header has wrong denomination");
  const auto outputs=network.findChild<QTreeWidget*>("pgn-explorer-outputs")->findItems("Output 0",Qt::MatchExactly);
  require(outputs.size()==1&&outputs.first()->text(1)==(unit.isEmpty()?QString("—"):QString("0.0000000000 ")+unit),"Transaction detail output has wrong denomination");
  require(network.property("pgn_explorer_selected_block").toJsonObject()==block(100,a),"Currency presentation changed native block values");
  if(argc>1&&chain=="test"){network.resize(820,760);network.show();app.processEvents();network.grab().save(QString::fromLocal8Bit(argv[1])+"/explorer-testnet-820.png");}
 }
 view->inspect("42");require(heights.back()==42,"Height search did not use the height lookup");details.back()({},"Unavailable");require(view->property("pgn_explorer_selected_block").toJsonObject().value("hash")==b,"Failed query erased last valid details");
 view->findChild<QPushButton*>("pgn-explorer-latest")->click();auto olderPage=pages.back();view->findChild<QPushButton*>("pgn-explorer-latest")->click();auto newestPage=pages.back();newestPage(page(103,b),{});olderPage(page(50,a),{});require(table->item(0,0)->text()=="103","Late page replaced current page");
 auto two=page(103,b);auto rows=two.value("blocks").toArray();auto bottom=block(92,a);bottom["nTx"]=1;rows.append(bottom);two["blocks"]=rows;view->presentSnapshot(two);view->findChild<QPushButton*>("pgn-explorer-older")->click();require(heights.back()==91,"Paging skipped unseen blocks after a short latest page");
 auto pendingPage=pages.back();pendingPage(two,{});
 for(const QSize size:{QSize(820,700),QSize(1380,900),QSize(1920,1080)}){view->resize(size);app.processEvents();auto* split=view->findChild<QSplitter*>();require(split&&split->orientation()==(size.width()<1120?Qt::Vertical:Qt::Horizontal),"Explorer did not adapt to compact window");require(split->geometry().bottom()<=view->height(),"Explorer exceeded window height");require(table->horizontalScrollBar()->maximum()==0,"Block list clipped its columns horizontally");if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));QImage frame(view->size(),QImage::Format_ARGB32);view->render(&frame);require(frame.save(QString::fromLocal8Bit(argv[1])+QString("/explorer-%1.png").arg(size.width())),"Fixture screenshot failed");}}
 // A hash search falls back only on a native missing-block response.
 const auto txid=QString(64,'f');view->inspect(txid);const auto beforeTx=txReplies.size();details.back()({},"Core RPC error (-5): Block not found");
 require(txReplies.size()==beforeTx+1&&txids.back()==txid&&txBlocks.back().isEmpty(),"Universal hash search did not find a transaction through native fallback");
 txReplies.back()(txFixture(txid,a),{});require(view->property("pgn_explorer_selected_transaction").toJsonObject().value("txid")==txid&&view->property("pgn_explorer_selected_block").toJsonObject().isEmpty(),"Transaction selection retained stale block identity");
 auto* overview=view->findChild<QTreeWidget*>("pgn-explorer-overview");const auto amount=overview->findItems("Total outputs",Qt::MatchExactly);require(amount.size()==1&&amount[0]->text(1)=="0.0000000000 PGN","Transaction summary lost exact amount or network unit");
 auto* outputs=view->findChild<QTreeWidget*>("pgn-explorer-outputs");const auto input=outputs->findItems("Input 0",Qt::MatchExactly);require(input.size()==1&&input[0]->data(0,Qt::UserRole).toString()==QString(64,'c'),"Input transaction link missing");
 const auto txCalls=txReplies.size();view->presentSnapshot(page(105,b));require(txReplies.size()==txCalls&&view->property("pgn_explorer_selected_transaction").toJsonObject().value("txid")==txid&&view->findChild<QLabel*>("pgn-explorer-status")->text().startsWith("Native transaction"),"Chain refresh replaced the selected transaction");
 if(argc>1){view->resize(1380,900);app.processEvents();view->grab().save(QString::fromLocal8Bit(argv[1])+"/transaction-1380.png");view->resize(820,760);app.processEvents();view->grab().save(QString::fromLocal8Bit(argv[1])+"/transaction-820.png");}
 const auto blockLink=overview->findItems("Block hash",Qt::MatchExactly);require(blockLink.size()==1&&blockLink[0]->data(0,Qt::UserRole).toString()==a,"Transaction block navigation link missing");
 overview->itemDoubleClicked(blockLink[0],1);details.back()(block(100,a),{});require(view->property("pgn_explorer_selected_transaction").toJsonObject().isEmpty()&&view->property("pgn_explorer_selected_block").toJsonObject().value("hash")==a,"Block navigation did not exit transaction mode");
 view->inspect(txid);auto obsoleteBlock=details.back();view->inspectHash(b);details.back()(block(101,b),{});const auto ignoredCount=txReplies.size();obsoleteBlock({},"Core RPC error (-5): Block not found");require(txReplies.size()==ignoredCount,"Superseded block query started a stale transaction request");
 view->inspectTransaction(txid,a);auto obsoleteTx=txReplies.back();require(txBlocks.back()==a,"Explicit transaction block anchor lost");view->inspectHash(b);details.back()(block(101,b),{});obsoleteTx(txFixture(txid,a),{});require(view->property("pgn_explorer_selected_block").toJsonObject().value("hash")==b&&view->property("pgn_explorer_selected_transaction").toJsonObject().isEmpty(),"Late transaction overwrote newer block selection");
 view->inspect(txid);auto cancelled=details.back();view->inspect("invalid");const auto cancelCount=txReplies.size();cancelled({},"Core RPC error (-5): Block not found");require(txReplies.size()==cancelCount,"Invalid new query did not invalidate old pending query");
 view->inspect(txid);const auto noFallback=txReplies.size();details.back()({},"Core connection changed");require(txReplies.size()==noFallback,"Transport/epoch failure was misclassified as missing block");
 view->inspectTransaction(txid);txReplies.back()({},"Historical transaction lookup needs the Core transaction index");require(view->findChild<QLabel*>("pgn-explorer-status")->text().contains("transaction index")&&view->property("pgn_explorer_selected_block").toJsonObject().value("hash")==b,"Missing index erased last valid detail or hid the error");
 view->inspectTransaction(txid);auto destroyedTx=txReplies.back();view->inspectHash(a);auto afterDestruction=details.back();delete view;app.processEvents();afterDestruction(block(100,a),{});destroyedTx(txFixture(txid,a),{});

 // Native bounded pages stay readable and preserve every transaction/output row.
 bool defer=false;std::vector<BlockExplorerServices::Reply> deferred;
 BlockExplorerServices projected{
  [&](QString hash,auto done){done(projectionBlock(hash,0),{});},
  [&](int,auto done){done(projectionBlock(a,0),{});},
  [&](int,auto done){done(page(100,a),{});},
  [&](QString id,QString hash,auto done){done(projectionTx(id,hash,0),{});},
  [&](QString hash,int offset,auto done){if(defer)deferred.push_back([hash,offset,done](QJsonObject,QString){done(projectionBlock(hash,offset),{});});else done(projectionBlock(hash,offset),{});},
  [&](QString id,QString hash,int offset,auto done){done(projectionTx(id,hash,offset),{});}};
 BlockExplorerView bounded(projected,nullptr,"test");bounded.resize(820,760);bounded.show();bounded.presentSnapshot(page(100,a));
 auto* txTable=bounded.findChild<QTableWidget*>("pgn-explorer-transactions");auto* pageNext=bounded.findChild<QPushButton*>("pgn-explorer-detail-page-next");auto* pagePrevious=bounded.findChild<QPushButton*>("pgn-explorer-detail-page-previous");
 require(txTable->rowCount()==25&&pageNext->isEnabled()&&!pagePrevious->isEnabled(),"Bounded block page did not expose navigation");QStringList allIDs;
 for(int offset:{0,25,50}){require(bounded.property("pgn_explorer_selected_block").toJsonObject().value("tx_offset")==offset,"Block page offset changed");for(int row=0;row<txTable->rowCount();++row){require(txTable->item(row,0)->text()==QString::number(offset+row),"Block transaction index reset across pages");allIDs.push_back(txTable->item(row,1)->text());}if(offset<50)pageNext->click();}
 require(allIDs.size()==51&&QSet<QString>(allIDs.begin(),allIDs.end()).size()==51&&!pageNext->isEnabled(),"Block pagination lost or duplicated transactions");pagePrevious->click();require(txTable->item(0,0)->text()=="25","Previous block transaction page skipped rows");
 txTable->cellClicked(0,0);require(!bounded.property("pgn_explorer_selected_transaction").toJsonObject().isEmpty(),"Projected block transaction row did not open anchored transaction details");
 auto* tabs=bounded.findChild<QTabWidget*>("pgn-explorer-detail-tabs");tabs->setCurrentIndex(1);pageNext->click();require(bounded.property("pgn_explorer_selected_transaction").toJsonObject().value("io_offset")==25&&tabs->currentIndex()==1,"Input/output page navigation reset the user's tab");
 auto* outTree=bounded.findChild<QTreeWidget*>("pgn-explorer-outputs");require(outTree->findItems("Output 25",Qt::MatchExactly).size()==1&&outTree->findItems("Output 49",Qt::MatchExactly).size()==1,"Output page indices were relabeled");
 const auto previews=outTree->findItems("Script preview",Qt::MatchStartsWith|Qt::MatchRecursive);require(previews.size()==25&&previews[0]->text(0).contains("500000")&&previews[0]->text(1).endsWith("…")&&previews[0]->text(1).size()==193,"Long script preview was unbounded or silently presented as complete");
 const auto exactTotal=bounded.findChild<QTreeWidget*>("pgn-explorer-overview")->findItems("Total outputs",Qt::MatchExactly);require(exactTotal.size()==1&&exactTotal[0]->text(1)=="0.0000000000 tPGN","Projected transaction total lost testnet denomination");
 if(argc>1){for(int width:{820,1380}){bounded.resize(width,760);app.processEvents();bounded.grab().save(QString::fromLocal8Bit(argv[1])+QString("/bounded-transaction-%1.png").arg(width));}}
 pageNext->click();require(outTree->findItems("Output 52",Qt::MatchExactly).size()==1&&!pageNext->isEnabled(),"Final output page is incomplete or skips its end");
 bounded.inspectHash(a);defer=true;pageNext->click();require(!deferred.empty(),"Deferred block page fixture did not start");bounded.inspectHash(b);deferred.back()({},{});require(bounded.property("pgn_explorer_selected_block").toJsonObject().value("hash")==b,"Late transaction-page response overwrote newer block selection");
 std::cout<<"PASS explorer: latest intent wins, stable refresh/selection, exact amounts, non-skipping paging, compact/wide layout and destroyed-view callback safety\n";
}
