#include "ui/wallet_console_view.h"
#include "qt_offscreen_fixture.h"
#include <QApplication>
#include <QJsonArray>
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
 method->setText("echojson");params->setPlainText(QString::fromUtf8(exact));scope->setCurrentIndex(1);view->execute();view->execute();
 require(calls==1&&receivedMethod=="echojson"&&receivedParams==exact&&receivedWallet&&!execute->isEnabled(),"Duplicate request, wrong scope or rounded parameters");
 reply("{\"result\":[1530002999806485841567],\"id\":\"unique\"}",{});
 require(execute->isEnabled()&&output->toPlainText().contains("1530002999806485841567")&&!output->toPlainText().contains("comma,colon"),"Response rounded or input parameters leaked to history");
 view->execute();reply({},"Core connection changed; check command outcome");require(status->text().contains("Command failed")&&execute->isEnabled(),"Failure hid outcome or latched controls");
 for(int i=0;i<5;++i){view->execute();reply("{\"result\":\""+QByteArray(220000,'x')+"\"}",{});require(output->toPlainText().size()<524288,"Unbounded console history");}
 clear->click();require(output->toPlainText().isEmpty()&&params->toPlainText()=="[]","Clear retained visible input/output");
 view->execute();delete view;reply("{\"result\":null}",{});
 std::cout<<"PASS explicit execution, exact JSON integers, scope, no duplicate/replayed commands, bounded history, errors and late lifetime\n";
}
