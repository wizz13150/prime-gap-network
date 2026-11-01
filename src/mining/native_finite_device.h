#pragma once
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace pgn::mining {
// The existing, bounded 3060 science-test profile only. These are protocol
// compatibility constants, NOT allocation, network credit or physical trust.
inline constexpr char Finite3060Device[]="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
inline constexpr char Finite3060Controller[]="C:/PGN/runtime/devices/test-3060/native-test-control.ps1";
inline constexpr char Finite3060Start[]="199999990000000000000";
inline constexpr char Finite3060Length[]="1030792151040";
inline constexpr char Finite3060ImageSha256[]="A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43";

// Explicit caller authority to latch this configured card/profile OFF. This
// is NOT a run/process identity, observed stop, or fallback for a stale run.
enum class Configured3060OffReason { Unspecified, ApplicationClose, ExplicitCardOff };

struct NativeFiniteManifest {
    QString run,pid,birth,imageSha256,start,length,stopPath,stdoutPath,stderrPath,requestId;
};
struct NativeFiniteExit { QString run,utc; qint32 exitCode{}; };
struct NativeFiniteError { QString utc,error; };
struct NativeFiniteObservation {
    QString device,scope,observedUtc;
    bool owned{},desiredEnabled{},stopRequested{};
    std::optional<NativeFiniteManifest> manifest;
    std::optional<NativeFiniteExit> lastExit;
    std::optional<NativeFiniteError> lastError;
    // Raw reported state retained. No derived "GPU idle", completed test,
    // checkpoint, fresh acquisition or command acknowledgement is provided.
    QJsonObject packet;
};

// Internal typed projection of an ALREADY strict-scanned complete response.
// NativeControlCommand's one bounded JSON walker must reject duplicate decoded
// keys and provide original scalar lexemes before calling this function.
// A QJsonObject alone cannot prove absence of duplicate/rounded wire fields.
// Throws on absent/unknown/incompatible fields; no default observation.
NativeFiniteObservation projectNativeFiniteObservation(
    const QJsonObject&,const QHash<QString,QByteArray>& scalarLexemes);
}
