#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>bool waitFor(F predicate){QElapsedTimer age;age.start();while(!predicate()&&age.elapsed()<1000){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}return predicate();}

// Only a unique, test-owned local pipe. Never a PGN application endpoint.
struct OwnPipe {
    QTemporaryDir files;QLocalServer server;QLocalSocket client;std::unique_ptr<QLocalSocket> peer;
    OwnPipe(){
        need(files.isValid(),"own temporary directory");
        server.setSocketOptions(QLocalServer::UserAccessOption);
        const auto name="pgn-closed-readyread-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        need(server.listen(name),"listen on unique own test pipe");client.connectToServer(name);
        need(client.waitForConnected(1000),"connect only own test client");
        if(!server.hasPendingConnections())need(server.waitForNewConnection(1000),"accept own test client");
        peer.reset(server.nextPendingConnection());need(bool(peer),"own accepted socket");
    }
    void prefill(const QByteArray& bytes){
        need(client.write(bytes)==bytes.size(),"own exact test write");client.flush();
        need(waitFor([&]{return peer->bytesAvailable()==bytes.size();}),"preload bytes before installing readyRead handler");
    }
    void tail(const QByteArray& bytes){need(client.write(bytes)==bytes.size(),"own exact suffix write");client.flush();}
};

void missedEdge(){
    OwnPipe p;p.prefill("inspect\n");
    check(p.peer->canReadLine(),"complete command is present before readyRead connection");
    int callbacks{};QList<QByteArray> commands;
    QObject::connect(p.peer.get(),&QLocalSocket::readyRead,p.peer.get(),[&]{++callbacks;if(p.peer->canReadLine())commands.append(p.peer->readLine(64));});
    QEventLoop loop;QTimer::singleShot(40,&loop,&QEventLoop::quit);loop.exec();
    check(callbacks==0,"readyRead connection does not replay the already-emitted edge");
    check(commands.isEmpty()&&p.peer->bytesAvailable()==8,"unread complete command remains without a new event");
    std::cout<<"BASELINE prefilled=8 callbacks="<<callbacks<<" commands="<<commands.size()<<'\n';
    p.tail("x");check(waitFor([&]{return commands.size()==1;}),"one later byte causes a fresh readyRead edge");
    check(commands==QList<QByteArray>{"inspect\n"},"late edge drains the old exact command, not a fabricated retry");
    check(p.peer->readAll()=="x","new byte itself was not the old complete command");
}

void queuedInitialDrain(){
    OwnPipe p;p.prefill("inspect\n");int readyEdges{},drains{};QList<QByteArray> commands;
    const QPointer<QLocalSocket> socket(p.peer.get());
    const auto drain=[&]{++drains;if(socket&&socket->canReadLine())commands.append(socket->readLine(64));};
    QObject::connect(p.peer.get(),&QLocalSocket::readyRead,p.peer.get(),[&]{++readyEdges;drain();});
    check(QMetaObject::invokeMethod(p.peer.get(),drain,Qt::QueuedConnection),"queue the same drain after connecting readyRead");
    check(drains==0&&commands.isEmpty(),"initial drain is genuinely queued, not reentrant");
    check(waitFor([&]{return commands.size()==1;}),"queued drain handles bytes that were already present");
    check(commands==QList<QByteArray>{"inspect\n"}&&p.peer->bytesAvailable()==0,"exact command consumed once with no extra client write");
    check(readyEdges==0&&drains==1,"repair needs no repeated readyRead signal or retry");
    QCoreApplication::processEvents();check(commands.size()==1&&drains==1,"event loop does not duplicate the command");
    std::cout<<"QUEUED prefilled=8 readyReadSignals="<<readyEdges<<" drains="<<drains<<" commands="<<commands.size()<<'\n';
}

void fragmentedAndOwnerLoss(){
    {
        OwnPipe p;p.prefill("ins");int drains{};QList<QByteArray> commands;
        const QPointer<QLocalSocket> socket(p.peer.get());
        const auto drain=[&]{++drains;if(socket&&socket->canReadLine())commands.append(socket->readLine(64));};
        QObject::connect(p.peer.get(),&QLocalSocket::readyRead,p.peer.get(),drain);
        check(QMetaObject::invokeMethod(p.peer.get(),drain,Qt::QueuedConnection),"fragment initial drain queued");
        check(waitFor([&]{return drains==1;}),"fragment initial drain actually executed");
        check(commands.isEmpty()&&p.peer->bytesAvailable()==3,"initial drain cannot consume an incomplete command");
        p.tail("pect\n");check(waitFor([&]{return commands.size()==1;}),"later suffix uses the normal readyRead path");
        check(commands==QList<QByteArray>{"inspect\n"}&&p.peer->bytesAvailable()==0,"fragment and suffix preserve exact command once");
    }
    {
        OwnPipe p;p.prefill("inspect\n");int calls{};
        check(QMetaObject::invokeMethod(p.peer.get(),[&]{++calls;},Qt::QueuedConnection),"owner-bound initial drain queued");
        p.peer.reset();QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();
        check(calls==0,"destroyed socket cancels its queued initial drain");
    }
}
}

int main(int argc,char** argv){QCoreApplication app(argc,argv);try{missedEdge();queuedInitialDrain();fragmentedAndOwnerLoss();}catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"OWNED_IPC_READY_READ "<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;}
