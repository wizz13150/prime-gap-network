#pragma once
#include "wallet/core_console_rpc.h"
#include "ui/core_console_commands.h"
#include <QCompleter>
#include <QStringListModel>
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QComboBox>
#include <QJsonArray>
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
// Completion selection never emits returnPressed or runs an RPC command.
class ConsoleMethodEdit final:public QLineEdit {
public: using QLineEdit::QLineEdit;
protected:
 bool event(QEvent* event)override{
  if(event->type()==QEvent::KeyPress&&completer()&&completer()->popup()->isVisible()){
   auto* key=static_cast<QKeyEvent*>(event);
   if(key->key()==Qt::Key_Tab||key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){
    const auto index=completer()->popup()->currentIndex();
    const auto text=index.isValid()?index.data(Qt::EditRole).toString():completer()->currentCompletion();
    if(!text.isEmpty())setText(text);
    completer()->popup()->hide();key->accept();return true;
   }
  }
  return QLineEdit::event(event);
 }
};
class WalletConsoleView final:public QWidget {
public:
 using Reply=std::function<void(QByteArray,QString)>;
 using Request=std::function<void(QString,QByteArray,bool,Reply)>;
 explicit WalletConsoleView(Request request,QWidget* parent=nullptr):QWidget(parent),request_(std::move(request)){
  auto* box=new QVBoxLayout(this);box->setContentsMargins(8,8,8,8);
  auto* row=new QHBoxLayout;
  scope_=new QComboBox;scope_->setObjectName("pgn-console-scope");scope_->addItems({"Node","Displayed wallet"});row->addWidget(scope_);
  method_=new ConsoleMethodEdit;method_->setObjectName("pgn-console-method");method_->setPlaceholderText("Core RPC method · getblockchaininfo");method_->setMaxLength(64);row->addWidget(method_,1);
  completionModel_=new QStringListModel(bundledCoreConsoleCommands(),this);
  auto* complete=new QCompleter(completionModel_,method_);complete->setObjectName("pgn-console-completer");
  complete->setCaseSensitivity(Qt::CaseInsensitive);complete->setFilterMode(Qt::MatchStartsWith);
  complete->setModelSorting(QCompleter::CaseInsensitivelySortedModel);complete->setMaxVisibleItems(10);
  complete->setCompletionMode(QCompleter::PopupCompletion);method_->setCompleter(complete);
  complete->popup()->setStyleSheet("QAbstractItemView{background:#08232b;color:#dffff7;border:1px solid #387b78;selection-background-color:#17584f;selection-color:#ffffff;padding:3px;}");
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
  const bool fullHelp=method=="help"&&QJsonDocument::fromJson(parameters).array().isEmpty();
  request_(method,parameters,wallet,[weak,method,wallet,fullHelp](QByteArray bytes,QString error){
   if(!weak)return;weak->setBusy(false);weak->status_->setText(error.isEmpty()?"Command completed":"Command failed: "+error.left(1000));
   if(error.isEmpty()&&fullHelp){
    const auto result=QJsonDocument::fromJson(bytes).object().value("result");
    if(result.isString()&&result.toString().size()<=262144){
     QStringList names;
     const QRegularExpression pattern("^([a-z][a-z0-9_]{0,63})(?:\\s|$)");
     for(const auto& line:result.toString().split('\n')){const auto match=pattern.match(line);if(match.hasMatch())names.append(match.captured(1));if(names.size()>1024)break;}
     if(!names.isEmpty()&&names.size()<=1024){names.removeDuplicates();names.sort(Qt::CaseInsensitive);weak->completionModel_->setStringList(names);}
    }
   }
   const auto entry=QString("> %1 · %2\n").arg(method,wallet?"Displayed wallet":"Node")+(error.isEmpty()?pgn::wallet::formatConsoleJson(bytes):"Error: "+error.left(1000));
   if(weak->output_->toPlainText().size()+entry.size()>524288){weak->output_->clear();weak->output_->appendPlainText("Earlier output cleared to keep this session within its memory limit.");}
   weak->output_->appendPlainText(entry);
  });
 }
private:
 void setBusy(bool busy){busy_=busy;for(auto* widget:std::array<QWidget*,5>{scope_,method_,execute_,clear_,parameters_})widget->setEnabled(!busy);}
 QStringListModel* completionModel_{};Request request_;QComboBox* scope_{};QLineEdit* method_{};QPlainTextEdit *parameters_{},*output_{};QLabel* status_{};QPushButton *execute_{},*clear_{};bool busy_{};
};
}
