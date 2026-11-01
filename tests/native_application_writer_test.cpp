#include "mining/native_application_writer.h"
#include <QCoreApplication>
#include <QThread>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace pgn::mining;
namespace {
int checks{},failures{};
void need(bool ok){++checks;if(!ok){++failures;std::cerr<<"writer check "<<checks<<" failed\n";}}
void pump(){for(int i=0;i<32;++i)QCoreApplication::processEvents();}
const QString closeId="10000000-0000-4000-8000-000000000001";
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    {
        // Exact scheduling seam of NativeApplicationRuntime::start(): profile
        // is queued, then finite observation; the profile continuation queues
        // the constant inventory BEFORE settling its own outer Read. The real
        // writer's FIFO therefore admits finite first. These are controlled
        // completions, NOT SSH/elapsed-time/actual-startup measurements.
        NativeApplicationWriter w;std::vector<int> order;NativeApplicationWriter::Done profile,finite;
        const auto complete=[&](auto result){need(result.entered&&result.error.isEmpty());};
        need(w.enqueue(NativeApplicationOperation::Read,[&](auto done){order.push_back(1);profile=done;},complete));
        need(w.enqueue(NativeApplicationOperation::Read,[&](auto done){order.push_back(2);finite=done;},complete));
        pump();need(order==std::vector<int>{1}&&bool(profile)&&!finite);
        need(w.enqueue(NativeApplicationOperation::Read,[&](auto done){order.push_back(3);done({});},complete));
        profile({});pump();need(order==std::vector<int>({1,2})&&bool(finite));
        pump();need(order==std::vector<int>({1,2})); // No snapshot entry before finite completion.
        finite({});pump();need(order==std::vector<int>({1,2,3}));
    }
    {
        NativeApplicationWriter w;std::vector<int> order;NativeApplicationWriter::Done first;
        need(w.enqueue(NativeApplicationOperation::Mutation,[&](auto done){order.push_back(1);first=done;},[&](auto r){need(r.entered&&r.error.isEmpty());order.push_back(2);}));
        need(w.enqueue(NativeApplicationOperation::Mutation,[&](auto done){order.push_back(3);done({});},[&](auto r){need(!r.entered&&!r.error.isEmpty());order.push_back(4);}));
        pump();need(order==std::vector<int>{1});
        need(w.prepareClose(closeId,[&](auto r){need(r.entered&&r.error.isEmpty());order.push_back(5);}));
        need(w.frozen()&&w.closeRequestId()==closeId);need(!w.prepareClose(closeId,[](auto){}));
        need(!w.enqueue(NativeApplicationOperation::Mutation,[](auto done){done({});},[](auto){}));
        need(w.enqueue(NativeApplicationOperation::Stop,[&](auto done){order.push_back(6);done({});done("duplicate");},[&](auto r){need(r.entered&&r.error.isEmpty());order.push_back(7);}));
        need(w.enqueue(NativeApplicationOperation::Read,[&](auto done){order.push_back(8);done({});},[&](auto r){need(r.entered);order.push_back(9);}));
        pump();need(order==std::vector<int>{1});first({});first("late duplicate");pump();
        need(order==std::vector<int>({1,2,4,5,6,7,8,9}));
        bool replay=false;need(w.prepareClose(closeId,[&](auto r){replay=r.entered;}));need(!replay);pump();need(replay);
        need(!w.prepareClose("10000000-0000-4000-8000-000000000002",[](auto){}));
    }
    {
        NativeApplicationWriter w;int work{},refused{},barrier{};
        for(int i=0;i<NativeApplicationWriter::PendingLimit;++i)need(w.enqueue(NativeApplicationOperation::Mutation,[&](auto done){++work;done({});},[&](auto r){need(!r.entered);++refused;}));
        need(!w.enqueue(NativeApplicationOperation::Read,[](auto done){done({});},[](auto){}));
        need(w.prepareClose(closeId,[&](auto r){need(r.entered);++barrier;}));pump();
        // One drain per queued Qt event; bound is command admission, not data.
        for(int i=0;i<10&&barrier==0;++i)pump();
        need(work==0&&refused==NativeApplicationWriter::PendingLimit&&barrier==1);
    }
    {
        NativeApplicationWriter w;int finished{};
        need(w.enqueue(NativeApplicationOperation::Read,[](auto){throw std::runtime_error("bounded failure");},[&](auto r){need(r.entered&&r.error=="bounded failure");++finished;}));
        need(w.enqueue(NativeApplicationOperation::Read,[](auto done){std::thread([done]{done({});}).join();},[&](auto r){need(r.entered&&r.error.isEmpty()&&QThread::currentThread()==w.thread());++finished;throw 7;}));
        need(w.enqueue(NativeApplicationOperation::Read,[](auto done){done({});},[&](auto r){need(r.entered);++finished;}));pump();need(finished==3);
        for(const auto& bad:QStringList{"",closeId.toUpper()+"x","00000000-0000-0000-0000-000000000000",closeId+"\n"})need(!w.prepareClose(bad,[](auto){}));
        need(!w.enqueue(static_cast<NativeApplicationOperation>(99),[](auto){},[](auto){}));need(!w.enqueue(NativeApplicationOperation::Read,{},[](auto){}));
    }
    {
        NativeApplicationWriter::Done late;int entered{},completed{};
        auto w=std::make_unique<NativeApplicationWriter>();need(w->enqueue(NativeApplicationOperation::Mutation,[&](auto done){++entered;late=done;},[&](auto){++completed;}));pump();need(entered==1);w.reset();late({});pump();need(completed==0);
    }
    {
        auto w=std::make_unique<NativeApplicationWriter>();int completed{};
        need(w->enqueue(NativeApplicationOperation::Read,[](auto done){done({});},[&](auto){++completed;w.reset();}));pump();need(!w&&completed==1);
    }
    std::cout<<"NATIVE_APPLICATION_WRITER "<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
