#pragma once
#include "wallet/exact_rpc_money.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTableWidget>
#include <QScrollBar>
#include <algorithm>
#include <vector>
namespace pgn::ui {
// PGN Core exposes the three-argument label/count/skip API. In particular,
// Bitcoin's optional fourth include-watch-only argument is not supported.
inline QJsonArray walletHistoryParameters(int skip=0){return {"*",50,std::max(0,skip)};}
inline qint64 walletHistoryTime(const QJsonObject& row){
 for(const auto* key:{"time","timereceived","blocktime"}){
  const auto value=row.value(key).toInteger(-1);
  if(value>0&&value<=253402300799LL)return value;
 }
 return 0;
}
inline QString walletHistoryOutput(const QJsonObject& row){return row.value("txid").toString()+":"+QString::number(row.value("vout").toInteger(-1))+":"+row.value("address").toString();}
inline bool presentWalletHistory(QTableWidget* table,const QJsonArray& input){
 if(input.size()>256)return false;
 std::vector<QJsonObject> rows;QSet<QString> sends,receives;
 for(const auto& value:input){if(!value.isObject())return false;const auto row=value.toObject();rows.push_back(row);const auto category=row.value("category").toString();if(!row.value("txid").toString().isEmpty()){if(category=="send")sends.insert(walletHistoryOutput(row));if(category=="receive")receives.insert(walletHistoryOutput(row));}}
 std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){
  const auto at=walletHistoryTime(a),bt=walletHistoryTime(b);if(at!=bt)return at>bt;
  if(a.value("txid")!=b.value("txid"))return a.value("txid").toString()<b.value("txid").toString();
  if(a.value("vout")!=b.value("vout"))return a.value("vout").toInteger()<b.value("vout").toInteger();
  return a.value("category").toString()<b.value("category").toString();
 });
 const int scroll=table->verticalScrollBar()->value();QString selected;
 if(table->currentRow()>=0&&table->item(table->currentRow(),0))selected=table->item(table->currentRow(),0)->data(Qt::UserRole+1).toString();
 table->setUpdatesEnabled(false);table->setRowCount(int(rows.size()));int selectedRow=-1;
 for(int i=0;i<int(rows.size());++i){const auto& row=rows[size_t(i)];const auto when=walletHistoryTime(row);const auto instant=QDateTime::fromSecsSinceEpoch(when);const auto category=row.value("category").toString();const auto output=walletHistoryOutput(row);const bool internal=sends.contains(output)&&receives.contains(output);QString type=category;
  if(category=="send")type=internal?"Self-transfer · sent":"Sent";
  else if(category=="receive")type=internal?"Self-transfer · received":"Received";
  else if(category=="generate")type="Mining reward";
  else if(category=="immature")type="Immature reward";
  else if(category=="orphan")type="Orphaned reward";
  const auto amount=wallet::exactAtomicField(row,"amount");const auto confirmations=row.value("confirmations").toInteger();
  const QStringList cells{when?instant.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"):QString("Time unavailable"),type,row.value("address").toString(),amount?wallet::formatAtomicPgn(*amount):QString("—"),confirmations<0?QString("Conflicted"):confirmations==0?QString("Unconfirmed"):QString::number(confirmations)};
  QString tooltip="Transaction: "+row.value("txid").toString();if(when)tooltip+="\nUTC: "+instant.toUTC().toString(Qt::ISODate);if(const auto fee=wallet::exactAtomicField(row,"fee"))tooltip+="\nFee: "+wallet::formatAtomicPgn(*fee);if(internal)tooltip+="\nCore reports the debit and credit separately; both entries are retained.";
  const auto identity=output+":"+category;
  for(int c=0;c<cells.size();++c){auto* item=table->item(i,c);if(!item){item=new QTableWidgetItem;table->setItem(i,c,item);}if(item->text()!=cells[c])item->setText(cells[c]);item->setToolTip(tooltip);if(c==0){item->setData(Qt::UserRole,when);item->setData(Qt::UserRole+1,identity);}if(c==3)item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);}
  if(identity==selected)selectedRow=i;
 }
 if(selectedRow>=0)table->selectRow(selectedRow);table->verticalScrollBar()->setValue(scroll);table->setUpdatesEnabled(true);return true;
}
}
