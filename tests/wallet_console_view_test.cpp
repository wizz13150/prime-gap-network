#include "ui/wallet_console_view.h"
#include "qt_offscreen_fixture.h"
#include <QApplication>
#include <QJsonArray>
#include <QMouseEvent>
#include <QKeyEvent>
#include <iostream>
#include <stdexcept>
static void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
int main(int argc,char** argv){
 if(!pgn::test::prepareOffscreen())return 2;QApplication app(argc,argv);
 using namespace pgn::wallet;
 const QByteArray exact="[9007199254740993,1530002999806485841567,0.0000000001,\"comma,colon:brace}quote\\\"\"]";
 require(validateConsoleCommand("echojson",exact).isEmpty(),"Exact valid parameters rejected");
 for(const auto& bad:{QByteArray("{}"),QByteArray("["),QByteArray("[] {}"),QByteArray(65537,' ')})require(!validateConsoleCommand("echojson",bad).isEmpty(),"Malformed or oversized parameters accepted");
 require(!validateConsoleCommand("getinfo; stop", "[]").isEmpty(),"Command-language injection admitted");
 const auto wire=consoleRpcEnvelope("unique","echojson",exact);
 require(wire.contains(exact)&&wire.contains("9007199254740993")&&wire.contains("1530002999806485841567"),"Wire rounded exact integers");
 const auto formatted=formatConsoleJson(wire);
 require(formatted.contains("1530002999806485841567")&&QJsonDocument::fromJson(formatted.toUtf8())==QJsonDocument::fromJson(wire),"Presentation changed JSON semantics or exact numbers");
 pgn::ui::WalletConsoleView::Reply reply;int calls=0;QString receivedMethod;QByteArray receivedParams;bool receivedWallet=false;
 auto* view=new pgn::ui::WalletConsoleView([&](QString method,QByteArray params,bool wallet,auto done){++calls;receivedMethod=method;receivedParams=params;receivedWallet=wallet;reply=std::move(done);});
 auto* method=view->findChild<QLineEdit*>("pgn-console-method");auto* params=view->findChild<QPlainTextEdit*>("pgn-console-parameters");auto* scope=view->findChild<QComboBox*>("pgn-console-scope");auto* output=view->findChild<QPlainTextEdit*>("pgn-console-output");auto* status=view->findChild<QLabel*>("pgn-console-status");auto* execute=view->findChild<QPushButton*>("pgn-console-execute");auto* clear=view->findChild<QPushButton*>("pgn-console-clear");
 require(calls==0,"Opening console executed a command");view->execute();require(calls==0,"Empty method executed");
 auto* complete=method->completer();
 require(complete&&complete->model()->rowCount()>=100,"Missing command-name catalogue");
 view->resize(850,500);view->show();method->setFocus();app.processEvents();
 auto openCompletion=[&](const QString& prefix,const QString& choice){
  method->setText(prefix);complete->setCompletionPrefix(prefix);complete->complete();app.processEvents();
  const auto matches=complete->completionModel()->match(complete->completionModel()->index(0,0),Qt::EditRole,choice,1,Qt::MatchExactly);
  require(matches.size()==1,"Requested command missing from prefix completions");
  complete->popup()->setCurrentIndex(matches.first());
  require(complete->popup()->isVisible(),"Completion popup did not open");
  return matches.first();
 };
 openCompletion("getblo","getblockchaininfo");
 QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(method,&tab);app.processEvents();
 require(method->text()=="getblockchaininfo"&&calls==0&&params->toPlainText()=="[]"&&output->toPlainText().isEmpty(),"Tab completion executed or altered parameters/output");
 openCompletion("sto","stop");
 QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(method,&enter);app.processEvents();
 require(method->text()=="stop"&&calls==0&&!complete->popup()->isVisible(),"Enter executed the selected stop command");
 const auto selected=openCompletion("getwal","getwalletinfo");
 const auto local=complete->popup()->visualRect(selected).center();
 const auto global=complete->popup()->viewport()->mapToGlobal(local);
 QMouseEvent press(QEvent::MouseButtonPress,QPointF(local),QPointF(global),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
 QMouseEvent release(QEvent::MouseButtonRelease,QPointF(local),QPointF(global),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
 QApplication::sendEvent(complete->popup()->viewport(),&press);QApplication::sendEvent(complete->popup()->viewport(),&release);app.processEvents();
 require(method->text()=="getwalletinfo"&&calls==0,"Mouse completion executed a command or failed to insert it");
 method->setText("not_a_known_rpc");require(calls==0,"Unknown manual method triggered an implicit RPC");
 method->setText("echojson");params->setPlainText(QString::fromUtf8(exact));scope->setCurrentIndex(1);view->execute();view->execute();
 require(calls==1&&receivedMethod=="echojson"&&receivedParams==exact&&receivedWallet&&!execute->isEnabled(),"Duplicate request, wrong scope or rounded parameters");
 reply("{\"result\":[1530002999806485841567],\"id\":\"unique\"}",{});
 require(execute->isEnabled()&&output->toPlainText().contains("1530002999806485841567")&&!output->toPlainText().contains("comma,colon"),"Response rounded or input parameters leaked to history");
 view->execute();reply({},"Core connection changed; check command outcome");require(status->text().contains("Command failed")&&execute->isEnabled(),"Failure hid outcome or latched controls");
 method->setText("help");params->setPlainText("[]");view->execute();
 reply("{\"result\":\"== Test ==\\nhelp\\ngetfreshcommand parameter\\nstop\\n\"}",{});
 complete->setCompletionPrefix("getfresh");require(complete->currentCompletion()=="getfreshcommand","Explicit help did not refresh the in-memory command catalogue");
 for(int i=0;i<5;++i){view->execute();reply("{\"result\":\""+QByteArray(220000,'x')+"\"}",{});require(output->toPlainText().size()<524288,"Unbounded console history");}
 clear->click();require(output->toPlainText().isEmpty()&&params->toPlainText()=="[]","Clear retained visible input/output");
 view->execute();delete view;reply("{\"result\":null}",{});
 std::cout<<"PASS explicit execution, exact JSON integers, scope, no duplicate/replayed commands, bounded history, errors and late lifetime\n";
}
