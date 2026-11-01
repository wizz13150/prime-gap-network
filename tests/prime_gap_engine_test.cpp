#include "mining/prime_gap_engine.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(argc!=2)return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::ReadOnly)||file.size()>65536)return 3;
    const auto fixture=QJsonDocument::fromJson(file.readAll()).object();unsigned checks=0;
    auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("Prime-gap engine invariant failed at "+std::to_string(checks));};
    try{
        for(const auto entry:fixture.value("jobs").toArray()){
            const auto expected=entry.toObject();
            for(const int k:{8,16,32,64}){
                const auto job=pgn::mining::primeGapEngineJob(expected.value("start").toString(),expected.value("end").toString(),k);
                check(job.bits==expected.value("bits").toInt());check(QString::number(job.highWord)==expected.value("high"));check(QString::number(job.lowWord)==expected.value("low"));
                check(QString::number(job.minimumGap)==expected.value("min_gap"));check(job.length=="2000000000000");
                const auto args=pgn::mining::primeGapPlanArguments(job,0);check(args.first()=="--plan");check(args.contains(job.start));check(args.at(args.indexOf("--gap-min")+1)==QString::number(job.minimumGap));
                check(pgn::mining::primeGapImageProfile("/engine/bin/worker-k"+QString::number(k),k));check(!pgn::mining::primeGapImageProfile("/engine/bin/worker-k64",k==64?8:k));
            }
        }
        for(const auto entry:fixture.value("actual_plans").toArray()){
            const auto p=entry.toObject();const auto plan=p.value("plan").toObject();
            const auto job=pgn::mining::primeGapEngineJob(p.value("start").toString(),p.value("end").toString(),p.value("K").toInt(),quint32(plan.value("gap_min").toInt()));
            check(!pgn::mining::validatePrimeGapPlan(job,plan).isEmpty());
        }
        for(const auto entry:fixture.value("qualified_plans").toArray()){
            const auto p=entry.toObject();const auto actual=p.value("plan").toObject();
            const auto job=pgn::mining::primeGapEngineJob(p.value("start").toString(),p.value("end").toString(),p.value("K").toInt(),quint32(actual.value("gap_min").toInt()));
            check(pgn::mining::validatePrimeGapPlan(job,actual).isEmpty());
            for(const auto key:{"start","length","gap_min","profile_k","high_word"}){auto bad=actual;bad.insert(key,"wrong");check(!pgn::mining::validatePrimeGapPlan(job,bad).isEmpty());}
            auto bad=actual;bad.insert("fits",false);check(!pgn::mining::validatePrimeGapPlan(job,bad).isEmpty());
            bad=actual;bad.insert("required_device_bytes",1.5);check(!pgn::mining::validatePrimeGapPlan(job,bad).isEmpty());
            bad=actual;bad.insert("arithmetic_backend","generic");check(!pgn::mining::validatePrimeGapPlan(job,bad).isEmpty());
            bad=actual;bad.remove("optimized_domain_schema");check(!pgn::mining::validatePrimeGapPlan(job,bad).isEmpty());
            const auto run=pgn::mining::primeGapRunArguments(job,0,"C:/engine/checkpoint","C:/engine/gaps.log","C:/engine/stop.request",42);
            check(!run.contains("--owner-pid"));check(run.at(run.indexOf("--emit-min-gap")+1)==QString::number(job.minimumGap));
        }
        for(const auto entry:fixture.value("rejected_ranges").toArray()){
            const auto r=entry.toArray();bool rejected=false;
            try{pgn::mining::primeGapEngineJob(r[0].toString(),r[1].toString(),64);}catch(const std::exception&){rejected=true;}check(rejected);
        }
        for(const auto entry:fixture.value("policy_transitions").toArray()){
            const auto transition=entry.toObject();const auto first=transition.value("begin").toString(),end=transition.value("end").toString();
            const auto gap=quint32(transition.value("gap").toInt());
            check(pgn::mining::primeGapMinimumAt(first)==gap);
            check(pgn::mining::primeGapMinimumAt(pgn::booking::subtractUnsigned(end,"1"))==gap);
            check(pgn::mining::primeGapNextThreshold(first)==end);
        }
        auto job=pgn::mining::primeGapEngineJob("150000000000000000000","150000001000000000000",64);check(job.bits==68&&job.minimumGap==954);
        const auto tail=pgn::mining::primeGapEngineJob("184467440737095515160","184467440737095516160",64);check(tail.positions==1000);
        check(pgn::mining::primeGapReleaseArchitecture("F0B9F81B3F8B8316BEADAB8F04E90A85C412D6DABE2793548F05D66D1AF80DD5")==86);
        check(!pgn::mining::primeGapReleaseArchitecture("093C8037A6A8BF2082E5AC8A3F85A31B6D57AA6FBCAA741CB99F1FAE3845A91D"));
        bool rejected=false;try{pgn::mining::primeGapRunArguments(job,0,"relative","C:/engine/gaps","C:/engine/stop",42);}catch(const std::exception&){rejected=true;}check(rejected);
        rejected=false;try{pgn::mining::primeGapRunArguments(job,0,"C:/engine/state","C:/engine/gaps","C:/engine/stop",0);}catch(const std::exception&){rejected=true;}check(rejected);
        std::cout<<"Prime-gap engine contract PASS "<<checks<<" checks, all29 widths/all4 K +actual GPU plans\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
