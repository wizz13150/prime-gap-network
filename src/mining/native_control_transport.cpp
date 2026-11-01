#include "mining/native_control_transport.h"
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pgn::mining {
class NativeControlTransport::Impl {
public:
    NativeControlTransport* owner;
    QThread* const creatingThread=QThread::currentThread();
    QProcess client;
    QTimer startup,deadline;
    std::optional<NativeControlCommand> command;
    QByteArray output,errors;
    Completion completion;
    bool used{},done{},stderrOverflow{};
    std::optional<int> observedExit;

    explicit Impl(NativeControlTransport* object):owner(object){
        startup.setSingleShot(true);deadline.setSingleShot(true);
        QObject::connect(&startup,&QTimer::timeout,owner,[this]{finish(NativeControlTransportStatus::TimedOut,"Owned SSH client startup deadline expired; remote outcome UNKNOWN");});
        QObject::connect(&deadline,&QTimer::timeout,owner,[this]{finish(NativeControlTransportStatus::TimedOut,"Control deadline expired; remote outcome UNKNOWN, no retry");});
        QObject::connect(&client,&QProcess::started,owner,[this]{startup.stop();});
        QObject::connect(&client,&QProcess::readyReadStandardOutput,owner,[this]{drain(false);});
        QObject::connect(&client,&QProcess::readyReadStandardError,owner,[this]{drain(true);});
        QObject::connect(&client,&QProcess::errorOccurred,owner,[this](QProcess::ProcessError){
            if(!done)finish(NativeControlTransportStatus::Failed,"Owned SSH client error: "+client.errorString().left(300)+"; remote outcome UNKNOWN");
        });
        QObject::connect(&client,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,[this](int code,QProcess::ExitStatus status){
            observedExit=code;drain(false);if(done)return;drain(true);if(done)return;
            const auto detail=QString::fromUtf8(errors).trimmed().left(280);
            if(status!=QProcess::NormalExit||code!=0){finish(NativeControlTransportStatus::Failed,"Owned SSH client abnormal/nonzero exit; remote outcome UNKNOWN"+(detail.isEmpty()?QString{}:QString(" · ")+detail));return;}
            const auto decoded=command->decode(output,true,code,errors);
            if(decoded.outcome==NativeControlOutcome::Unconfirmed){finish(NativeControlTransportStatus::Failed,"Invalid/incomplete control response: "+decoded.error+(detail.isEmpty()?QString{}:QString(" · ")+detail));return;}
            finish(NativeControlTransportStatus::CompleteResponse,{},decoded);
        });
    }
    ~Impl(){done=true;startup.stop();deadline.stop();stopOwnedClient();}
    void threadGuard() const {
        if(QThread::currentThread()!=creatingThread||owner->thread()!=creatingThread)
            throw std::logic_error("Native control transport creating-thread ownership required");
    }
    void stopOwnedClient(){
        // ONLY this QProcess's client. No process enumeration, remote STOP,
        // miner PID, command retry, lookup by name, or external cancellation.
        client.disconnect(owner);
        if(client.state()!=QProcess::NotRunning){client.kill();client.waitForFinished(1000);}
    }
    void finish(NativeControlTransportStatus status,QString message,NativeControlResult control={}){
        if(done)return;done=true;startup.stop();deadline.stop();
        NativeControlTransportResult result;result.status=status;result.control=std::move(control);
        result.error=message.left(400);result.standardError=errors;result.stderrOverflow=stderrOverflow;result.localExitCode=observedExit;
        stopOwnedClient();output.clear();
        // Do not invoke user code from a QProcess signal/cleanup stack: a
        // completion may delete owner and its QProcess/timers immediately.
        // There is no member access in the callback after user code runs.
        auto callback=std::move(completion);QPointer<NativeControlTransport> weak(owner);
        QTimer::singleShot(0,owner,[weak,callback=std::move(callback),result=std::move(result)]{
            if(!weak)return;
            try{callback(result);}catch(...){/* No second completion/retry on consumer exception. */}
        });
    }
    void drain(bool standardError){
        if(done)return;client.setReadChannel(standardError?QProcess::StandardError:QProcess::StandardOutput);
        auto& target=standardError?errors:output;
        const auto limit=standardError?NativeControlCommand::StderrLimit:NativeControlCommand::ResponseLimit;
        while(client.bytesAvailable()>0){
            const auto room=limit-target.size();
            const auto bytes=client.read(std::min<qint64>(65536,qint64(room)+1));
            if(bytes.isEmpty())break;
            if(bytes.size()>room){
                if(standardError){errors+=bytes.left(room);stderrOverflow=true;}
                finish(NativeControlTransportStatus::Failed,standardError?
                    "Owned SSH stderr overflow (bounded prefix only); remote outcome UNKNOWN":
                    "Owned SSH stdout overflow; remote outcome UNKNOWN");return;
            }
            target+=bytes;
        }
    }
    void start(const NativeControlCommand& request,NativeControlTimeouts timeouts,Completion callback){
        threadGuard();
        if(used)throw std::logic_error("Native control transport is single-use; no retry/submission reuse");
        if(!callback||timeouts.startupMs<=0||timeouts.startupMs>MaximumStartupMs||timeouts.deadlineMs<=0||
           timeouts.deadlineMs>MaximumDeadlineMs||timeouts.startupMs>timeouts.deadlineMs)
            throw std::invalid_argument("Explicit bounded startup/deadline and completion required");
        used=true;command=request;completion=std::move(callback);
        client.setProcessChannelMode(QProcess::SeparateChannels);
        client.setStandardInputFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
        client.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){
            args->flags|=CREATE_NO_WINDOW;
            args->startupInfo->dwFlags|=STARTF_USESHOWWINDOW;args->startupInfo->wShowWindow=SW_HIDE;
        });
#endif
        client.setProgram(command->ssh().executable);client.setArguments(command->ssh().arguments);
        startup.start(timeouts.startupMs);deadline.start(timeouts.deadlineMs);
        client.start(QIODevice::ReadOnly);
    }
};
NativeControlTransport::NativeControlTransport(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this)){}
NativeControlTransport::~NativeControlTransport()=default;
void NativeControlTransport::start(const NativeControlCommand& command,NativeControlTimeouts timeouts,Completion callback){impl_->start(command,timeouts,std::move(callback));}
void NativeControlTransport::cancel(){impl_->threadGuard();if(impl_->used&&!impl_->done)impl_->finish(NativeControlTransportStatus::Cancelled,"Owned SSH client cancelled; remote outcome UNKNOWN (remote command NOT undone)");}
}
