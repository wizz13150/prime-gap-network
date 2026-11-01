#pragma once
#include "mining/prime_gap_engine.h"
#include <QByteArray>

namespace pgn::mining {
struct PrimeGapSourceCounters {quint64 time{},positions{},gaps{},tests{},batches{};};
// Bounded, logical producer grammar. Native endpoint proof labels are retained
// as source assertions; this parser never grants independent coverage/credit.
class PrimeGapSourceStream {
public:
    explicit PrimeGapSourceStream(PrimeGapEngineJob,PrimeGapSourceCounters baseline = {});
    void row(const QByteArray&);
    void finish(bool complete,const QString& checkpointLabel = {}) const;
    const PrimeGapSourceCounters& counters() const{return last_;}
    quint64 newRecords() const{return records_;}
    bool hasCounters() const{return countersSeen_;}
private:
    PrimeGapEngineJob job_;
    PrimeGapSourceCounters base_,last_;
    quint64 records_{},groupRecords_{},descriptors_{},slot_{},remainder_{};
    QString minimumLower_,maximumLower_,largestUpper_,secondUpper_,stopLabel_;
    bool header_{},countersSeen_{},summary_{},complete_{},stopped_{};
};
}
