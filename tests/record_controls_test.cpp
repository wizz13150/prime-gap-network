// Public fixtures only; private offscreen clipboard, URL sink, rejecting proxy.
#include "ui/record_submission_view.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QUrl>
#include <iostream>
#include <memory>
#include <stdexcept>
class UrlSink final:public QObject{Q_OBJECT
public:QList<QUrl> urls;
public slots:void capture(const QUrl& url){urls.append(url);}
};
int main(int argc,char** argv){
 qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);int checks=0;
 const auto need=[&](bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);};
 try{
  QTemporaryDir root;need(root.isValid(),"private root unavailable");QCoreApplication::setOrganizationName("PGN-QA");QCoreApplication::setApplicationName("record-controls");QStandardPaths::setTestModeEnabled(true);
  QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path()+"/settings");
  QTcpServer proxy;need(proxy.listen(QHostAddress::LocalHost,0),"rejecting loopback proxy unavailable");int requests=0;
  QObject::connect(&proxy,&QTcpServer::newConnection,&app,[&]{while(proxy.hasPendingConnections()){auto* s=proxy.nextPendingConnection();++requests;QObject::connect(s,&QTcpSocket::readyRead,s,[s]{s->readAll();s->write("HTTP/1.1 503 Test fixture denies downloads\r\nContent-Length:0\r\nConnection:close\r\n\r\n");s->disconnectFromHost();});QObject::connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);}});
  QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",proxy.serverPort()));UrlSink sink;QDesktopServices::setUrlHandler("https",&sink,"capture");
  const auto file=root.path()+"/data/testnet/record-reference.json";QDir().mkpath(QFileInfo(file).absolutePath());
  const QJsonObject ref{{"schema","prime-gap-record-reference-1"},{"supported_bits",QJsonArray{67,95}},{"retrieved_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"source_url","https://example.invalid/controlled-reference"},{"source_blob_sha1",QString(40,'a')},{"minimum_potential_gap_in_domain",6},{"known_starts",QJsonObject{{"1","2"},{"2","3"},{"4","7"},{"6","39614081257132168796771975167"}}},{"known_beyond_domain",QJsonArray{}}};
  const auto saved=QJsonDocument(ref).toJson();{QSaveFile f(file);need(f.open(QIODevice::WriteOnly)&&f.write(saved)==saved.size()&&f.commit(),"reference write failed");}
  std::unique_ptr<QWidget> view(pgn::ui::createRecordSubmissionView(root.path(),nullptr,true));view->resize(1000,180);view->show();
  auto* expand=view->findChild<QToolButton*>("pgn-expand-record-submissions");auto* text=view->findChild<QPlainTextEdit*>("pgn-admissible-record-text");auto* copy=view->findChild<QPushButton*>("pgn-copy-record-submissions");auto* check=view->findChild<QPushButton*>("pgn-check-record-submissions");auto* open=view->findChild<QPushButton*>("pgn-open-cloudygo");
  need(expand&&text&&copy&&check&&open,"record controls absent");need(!expand->isChecked()&&text->isHidden()&&!copy->isEnabled(),"initial fold/copy state wrong");
  int delivered=0;pgn::ui::bindRecordEligibility(view.get(),[&](const QJsonArray&){++delivered;});
  const QJsonObject packet{{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"record_pairs",QJsonArray{QJsonObject{{"gap",956},{"lower_prime","160696461657079905863"},{"upper_prime","160696461657079906819"}}}}};
  const auto wait=[&](auto condition){QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<10000){app.processEvents();QThread::msleep(1);}need(condition(),"asynchronous record deadline");};
  // Let zero-delay constructor timers run while ticket data is still absent.
  // Otherwise their later duplicate refresh can mask a failed packet retry.
  for(int i=0;i<5;++i)app.processEvents();
  pgn::ui::presentRecordTicketData(view.get(),packet);wait([&]{return copy->isEnabled()&&delivered>0;});need(text->isReadOnly()&&text->toPlainText().contains("160696461657079905863"),"verified text not delivered");
  expand->click();app.processEvents();need(expand->isChecked()&&!text->isHidden(),"expand failed");copy->click();need(QApplication::clipboard()->text()==text->toPlainText(),"copy differs from eligible text");open->click();need(sink.urls==QList<QUrl>{QUrl("https://primegaps.cloudygo.com/")},"submission-site URL incorrect");
  // Real shell buttons are taller than the text-only default style. A wrapped
  // status must not make the editor extend below its own panel after folding
  // or width changes. The whole-shell offscreen matrix also checks this inside
  // the actual parent layout, without touching desktop windows/preferences.
  view->setStyleSheet("QPushButton{min-height:36px;}");
  for(const int width:{778,1040,1900,520,778})for(const bool folded:{true,false}){
   expand->setChecked(!folded);view->resize(width,1);for(int n=0;n<4;++n)app.processEvents();
   need(text->isHidden()==folded,"width/fold changed editor visibility");
   for(auto* child:{static_cast<QWidget*>(expand),static_cast<QWidget*>(copy),static_cast<QWidget*>(check),static_cast<QWidget*>(open),static_cast<QWidget*>(text)})
    if(child->isVisible())need(view->rect().contains(QRect(child->mapTo(view.get(),QPoint(0,0)),child->size())),"record control clipped after width/fold change");
  }
  expand->setChecked(true);
  const auto value=text->toPlainText();const int prior=delivered;for(int n=0;n<100;++n){pgn::ui::presentRecordTicketData(view.get(),packet);app.processEvents();}need(delivered==prior&&text->toPlainText()==value,"identical packets restart results");
  QJsonArray excess;for(int n=0;n<101;++n)excess.append(packet.value("record_pairs").toArray().first());
  for(const auto& bad:{QJsonObject{{"schema","PGN_HORIZON_MINING_VIEW_V1"}},QJsonObject{{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"record_pairs","invalid"}},QJsonObject{{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"record_pairs",excess}}}){
   pgn::ui::presentRecordTicketData(view.get(),bad);need(!copy->isEnabled(),"invalid packet admitted");pgn::ui::presentRecordTicketData(view.get(),packet);wait([&]{return copy->isEnabled();});need(text->toPlainText()==value,"retry changed verified copied data");
  }
  const int beforeRequests=requests;check->click();wait([&]{return requests>beforeRequests&&!view->property("pgn_record_reference_update_error").toString().isEmpty()&&check->isEnabled();});need(requests>beforeRequests,"forced download failure did not use local proxy");{QFile f(file);need(f.open(QIODevice::ReadOnly)&&f.readAll()==saved,"failed download replaced valid reference");}need(copy->isEnabled()&&text->toPlainText()==value,"failed download lost eligible data");
  expand->setChecked(false);need(text->isHidden(),"collapse failed");view.reset();view.reset(pgn::ui::createRecordSubmissionView(root.path(),nullptr,true));app.processEvents();need(!view->property("pgn_record_submissions_expanded").toBool(),"fold preference not retained");need(view->property("pgn_record_candidate_bytes").toInt()<=view->property("pgn_record_candidate_limit_bytes").toInt(),"candidate cache exceeds bound");
  view.reset();QDesktopServices::unsetUrlHandler("https");QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);std::cout<<"PASS "<<checks<<" real record control checks: certified956, fold/reopen, exact copy, intercepted URL, duplicates, download failure/cache retention, oversized-input recovery; no browser/GPU/public download/submission\n";
 }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#include "record_controls_test.moc"
