#pragma once
#include <QJsonObject>
#include <QString>
#include <optional>
#include <cstdint>

namespace pgn::mining {
enum class AdvanceOutcome { Success, UncertainTransport, Rejected };
struct AdvanceTicket {QString run;int attempt{};std::uint64_t generation{};};
struct AdvanceSettlement {bool stale{},retryable{},held{};int attempts{};};
// Pure admission gate: NEVER reserves, dispatches, starts or stops anything.
// A ticket is not a lease or authority to skip native ownership/start locks.
class CompletionAdvanceGate final {
public:
    explicit CompletionAdvanceGate(QString expectedDevice,qint64 retryDelayMs=10000,int maxAttempts=3);
    void reset();
    std::optional<AdvanceTicket> claim(const QJsonObject& snapshot,const QString& expectedRun,
        qint64 monotonicNowMs,bool enabled,bool operatorPaused,bool hasIncompleteSourceRow);
    AdvanceSettlement settle(const AdvanceTicket&,AdvanceOutcome,qint64 monotonicNowMs);
private:
    QString device_,run_,observed_;
    qint64 delay_{},retryAt_{};
    int maximum_{},attempts_{};bool inFlight_{},held_{};
    std::uint64_t generation_{};
};
bool completeSourceMatches(const QJsonObject&,const QString& expectedRun);
// Exact peer RAW-conservation ABI for one or more journals. Not image,
// physical ownership, independent byte/prime verification or network credit.
bool sourceLineageMatches(const QJsonObject&,const QString& terminal);
// Typed peer-verifier ABI, not independent byte/ownership/prime verification.
bool sourceResumeLineageMatches(const QJsonObject&,const QString& terminal);
}
