#include "ui/wallet_history_view.h"
#include "wallet/core_rpc.h"
#include <QApplication>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QApplication app(argc,argv);int checks=0;const auto need=[&](bool value){++checks;if(!value)throw std::runtime_error("wallet history check "+std::to_string(checks));};
 need(pgn::ui::walletHistoryParameters()==QJsonArray({"*",50,0}));
 const QByteArray raw=R"({"result":[{"time":1794060475,"category":"generate","amount":171.0000000000,"confirmations":167,"txid":"reward","vout":1},{"time":1794079447,"category":"receive","address":"own","amount":80.0000000000,"confirmations":0,"txid":"self","vout":0},{"time":1794079447,"category":"send","address":"own","amount":-80.0000000000,"fee":-0.0000001530,"confirmations":0,"txid":"self","vout":0}],"error":null,"id":"test"})";
 const auto decoded=pgn::wallet::decodeCoreRpcResponse(raw,"test","listtransactions");need(decoded.error.isEmpty());
 QTableWidget table(0,5);need(pgn::ui::presentWalletHistory(&table,decoded.value.toArray()));need(table.rowCount()==3);
 need(table.item(0,0)->data(Qt::UserRole).toLongLong()==1794079447);need(table.item(2,0)->data(Qt::UserRole).toLongLong()==1794060475);
 need(table.item(0,1)->text()=="Self-transfer · received"&&table.item(1,1)->text()=="Self-transfer · sent");
 need(table.item(0,3)->text()=="80.0000000000"&&table.item(1,3)->text()=="-80.0000000000");need(table.item(1,3)->toolTip().contains("-0.0000001530"));
 need(table.item(0,4)->text()=="Unconfirmed"&&table.item(2,1)->text()=="Mining reward");
 for(int i=0;i<3;++i)need(!table.item(i,0)->text().isEmpty()&&!table.item(i,0)->text().startsWith("1970"));
 table.selectRow(1);need(pgn::ui::presentWalletHistory(&table,decoded.value.toArray()));need(table.currentRow()==1);
 need(!pgn::ui::presentWalletHistory(&table,QJsonArray{7}));need(table.rowCount()==3);
 auto missing=decoded.value.toArray().first().toObject();missing.remove("time");missing.insert("timereceived",1794079500);need(pgn::ui::walletHistoryTime(missing)==1794079500);missing.remove("timereceived");need(pgn::ui::walletHistoryTime(missing)==0);need(pgn::ui::presentWalletHistory(&table,QJsonArray{missing}));need(table.item(0,0)->text()=="Time unavailable");
 std::cout<<"PASS "<<checks<<" wallet history checks: actual self-transfer shape, three-argument native RPC, newest first, exact debit/credit/fee, missing time explicit, malformed reply preserves verified rows\n";
}
