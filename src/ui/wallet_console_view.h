#pragma once
#include "wallet/core_console_rpc.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>
#include <array>
namespace pgn::ui {
class WalletConsoleView final:public QWidget {
public:
 using Reply=std::function<void(QByteArray,QString)>;
 using Request=std::function<void(QString,QByteArray,bool,Reply)>;
 explicit WalletConsoleView(Request request,QWidget* parent=nullptr):QWidget(parent),request_(std::move(request)){
  auto* box=new QVBoxLayout(this);box->setContentsMargins(8,8,8,8);
  auto* row=new QHBoxLayout;
  scope_=new QComboBox;scope_->setObjectName("pgn-console-scope");scope_->addItems({"Node","Displayed wallet"});row->addWidget(scope_);
  method_=new QLineEdit;method_->setObjectName("pgn-console-method");method_->setPlaceholderText("Core RPC method · getblockchaininfo");method_->setMaxLength(64);row->addWidget(method_,1);
  execute_=new QPushButton("Execute");execute_->setObjectName("pgn-console-execute");row->addWidget(execute_);
  clear_=new QPushButton("Clear");clear_->setObjectName("pgn-console-clear");row->addWidget(clear_);box->addLayout(row);
  parameters_=new QPlainTextEdit("[]");parameters_->setObjectName("pgn-console-parameters");parameters_->setPlaceholderText("Parameters as a JSON array");parameters_->setMaximumHeight(90);parameters_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));box->addWidget(parameters_);
  status_=new QLabel("Commands run only when you choose Execute. History stays in this window.");status_->setObjectName("pgn-console-status");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);box->addWidget(status_);
  output_=new QPlainTextEdit;output_->setObjectName("pgn-console-output");output_->setReadOnly(true);output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));output_->setLineWrapMode(QPlainTextEdit::NoWrap);box->addWidget(output_,1);
  connect(execute_,&QPushButton::clicked,this,[this]{execute();});connect(method_,&QLineEdit::returnPressed,this,[this]{execute();});
  connect(clear_,&QPushButton::clicked,this,[this]{output_->clear();parameters_->setPlainText("[]");});
 }
 void execute(){
  if(busy_)return;const auto method=method_->text().trimmed();const auto parameters=parameters_->toPlainText().toUtf8();
  const auto error=pgn::wallet::validateConsoleCommand(method,parameters);if(!error.isEmpty()){status_->setText(error);return;}
  const bool wallet=scope_->currentIndex()==1;setBusy(true);status_->setText("Running "+method+"…");
  QPointer<WalletConsoleView> weak(this);
  request_(method,parameters,wallet,[weak,method,wallet](QByteArray bytes,QString error){
   if(!weak)return;weak->setBusy(false);weak->status_->setText(error.isEmpty()?"Command completed":"Command failed: "+error.left(1000));
   const auto entry=QString("> %1 · %2\n").arg(method,wallet?"Displayed wallet":"Node")+(error.isEmpty()?pgn::wallet::formatConsoleJson(bytes):"Error: "+error.left(1000));
   if(weak->output_->toPlainText().size()+entry.size()>524288){weak->output_->clear();weak->output_->appendPlainText("Earlier output cleared to keep this session within its memory limit.");}
   weak->output_->appendPlainText(entry);
  });
 }
private:
 void setBusy(bool busy){busy_=busy;for(auto* widget:std::array<QWidget*,5>{scope_,method_,execute_,clear_,parameters_})widget->setEnabled(!busy);}
 Request request_;QComboBox* scope_{};QLineEdit* method_{};QPlainTextEdit *parameters_{},*output_{};QLabel* status_{};QPushButton *execute_{},*clear_{};bool busy_{};
};
}
