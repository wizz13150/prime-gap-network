#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>bool waitFor(F predicate,int ms=3000){QElapsedTimer age;age.start();while(!predicate()&&age.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}return predicate();}

class Server final:public QLocalServer {
public:
    bool insideListen{};int incomingInsideListen{};
    bool start(const QString& name){insideListen=true;const bool ok=listen(name);insideListen=false;return ok;}
protected:
    void incomingConnection(quintptr descriptor)override{
        if(insideListen)++incomingInsideListen;
        QLocalServer::incomingConnection(descriptor);
    }
};

// A different test-owned thread pays the actual Windows connect/read path.
// It never blocks the server's event dispatch. No process/UI/user endpoint.
struct Client {
    std::unique_ptr<QThread> thread;QSemaphore release;
    std::atomic<bool> attempting{},connected{},finished{},cancelled{};
    bool reply{},secondReply{};QByteArray response;QString error;
    Client(QString name,QByteArray command,bool secondWrite=false){
        thread.reset(QThread::create([this,name,command,secondWrite]{
            try{
                QElapsedTimer age;age.start();std::unique_ptr<QLocalSocket> socket;attempting=true;
                // Startup connection polling only; no command is ever retried.
                while(!cancelled&&age.elapsed()<2000){
                    socket=std::make_unique<QLocalSocket>();socket->connectToServer(name);
                    if(socket->waitForConnected(100))break;
                    socket.reset();QThread::yieldCurrentThread();
                }
                if(!socket||socket->state()!=QLocalSocket::ConnectedState)throw std::runtime_error("own client never connected");
                connected=true;
                if(!release.tryAcquire(1,2000)||cancelled){finished=true;return;}
                if(socket->write(command)!=command.size())throw std::runtime_error("own exact command write");socket->flush();
                reply=socket->waitForReadyRead(300);if(reply)response+=socket->readAll();
                if(secondWrite&&!reply){
                    if(socket->write("\n",1)!=1)throw std::runtime_error("own second newline write");socket->flush();
                    secondReply=socket->waitForReadyRead(300);if(secondReply)response+=socket->readAll();
                }
            }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
            finished=true;
        }));
        thread->start();
    }
    ~Client(){cancelled=true;release.release();thread->wait(6000);}
    void send(){release.release();}
};

struct Result {int callbacks{},commands{},incomingDuringListen{};bool pendingBeforeHandler{},pendingAfterCurrent{},currentReply{},secondReply{},originalReply{};};

Result exercise(bool fixed){
    QTemporaryDir ownFiles;need(ownFiles.isValid(),"own temporary namespace");
    const auto name="pgn-closed-pending-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    Server server;server.setSocketOptions(QLocalServer::UserAccessOption);Result result;
    const auto accept=[&]{
        do{
            auto* socket=server.nextPendingConnection();if(!socket)return;++result.callbacks;
            const QPointer<QLocalSocket> guard(socket);
            const auto drain=[&,guard]{
                if(!guard||!guard->canReadLine())return;
                const auto command=guard->readLine(64);++result.commands;
                guard->write(command);guard->flush();
            };
            QObject::connect(socket,&QLocalSocket::readyRead,socket,drain);
            // Both variants already include the v7 read-buffer repair. The
            // only discriminant here is pending ACCEPT before its handler.
            QTimer::singleShot(0,socket,drain);
            QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            if(!fixed)break;
        }while(server.hasPendingConnections());
    };
    if(fixed)QObject::connect(&server,&QLocalServer::newConnection,&server,accept);
    Client original(name,"original\n");
    // Let the own thread reach connect polling before the real listen call.
    // No main-loop stall is used to manufacture the missing notification.
    QElapsedTimer scheduling;scheduling.start();while(!original.attempting&&scheduling.elapsed()<1000)QThread::yieldCurrentThread();
    need(original.attempting,"own connector ready before listen");
    need(server.start(name),"real SDK listener (same default backlog/UserAccessOption as Main)");
    result.incomingDuringListen=server.incomingInsideListen;
    result.pendingBeforeHandler=server.hasPendingConnections();
    need(result.incomingDuringListen>0,"connection actually accepted synchronously INSIDE listen (otherwise inconclusive)");
    if(!fixed)QObject::connect(&server,&QLocalServer::newConnection,&server,accept);
    need(waitFor([&]{return original.connected.load();}),"original own client connected");
    original.send();need(waitFor([&]{return original.finished.load();}),"original own response wait completed");
    need(original.error.isEmpty(),"original own client has no unrelated transport error");result.originalReply=original.reply;
    if(!fixed)need(result.callbacks==0&&result.pendingBeforeHandler,"late accept handler did not replay the synchronous newConnection");
    else need(original.reply&&original.response=="original\n","registered-before-listen handler serves the original command exactly");

    Client current(name,"current\n",!fixed);
    need(waitFor([&]{return current.connected.load();}),"new current client connected with normal QCore event dispatch");
    current.send();need(waitFor([&]{return current.finished.load();}),"current own bounded response wait completed");
    need(current.error.isEmpty(),"current own client has no unrelated transport error");
    result.currentReply=current.reply;result.secondReply=current.secondReply;result.pendingAfterCurrent=server.hasPendingConnections();
    if(fixed)need(current.reply&&current.response=="current\n"&&!result.pendingAfterCurrent,"register-before-listen/drain-all serves current client with no backlog lag");
    else need(!current.reply&&!current.secondReply&&result.callbacks==1&&result.pendingAfterCurrent,"one-per-notification consumes only the expired old peer; second write cannot wake current pending accept");
    std::cout<<(fixed?"FIXED":"BASELINE")<<" insideListen="<<result.incomingDuringListen<<" pendingBeforeHandler="<<result.pendingBeforeHandler
        <<" acceptCallbacks="<<result.callbacks<<" commands="<<result.commands<<" originalReply="<<result.originalReply
        <<" currentReply="<<result.currentReply<<" secondWriteReply="<<result.secondReply<<" pendingAfterCurrent="<<result.pendingAfterCurrent<<'\n';
    return result;
}
}

int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    const bool fixed=app.arguments().contains("--fixed");const auto result=exercise(fixed);
    std::cout<<"OWNED_IPC_PENDING_ACCEPT "<<checks<<" checks / 0 witness failures; current client would exit "<<(result.currentReply?0:4)<<'\n';
    // Baseline service failure is intentionally observable, not a test success
    // silently promoted to a working IPC service.
    return result.currentReply?0:4;
}catch(const std::exception& e){std::cerr<<"WITNESS_NOT_ESTABLISHED "<<e.what()<<'\n';return 2;}}
