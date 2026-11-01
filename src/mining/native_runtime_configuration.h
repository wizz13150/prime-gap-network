#pragma once
#include "mining/ssh_snapshot_transport.h"
#include <optional>

namespace pgn::mining {
struct NativeRuntimePaths {
    QString bookingDatabase,sourceHistoryDatabase,sourceMirrorDirectory,moduleRegistryDatabase;
    QString checkpointLabel; // REMOTE absolute Windows label; NEVER local stat/open
};
struct NativeRuntimeConfiguration {
    SshObservationEndpoint endpoint;
    NativeRuntimePaths paths;
    QString schema,sourcePath,sourceSha256,qualificationOpaque;
    QByteArray sourceUtf8; // exact original bounded bytes, never rewritten
    bool sourceOnly{false}; // explicit local managed-device domain; not remote idle evidence
};
struct NativeRuntimeConfigurationResult {
    std::optional<NativeRuntimeConfiguration> configuration;
    QString errorCode,error,sourcePath;
    std::optional<qint64> sourceBytes;
    std::optional<QByteArray> sourceUtf8; // bounded failed input also preserved
    std::optional<QString> sourceSha256; // absent if source not fully captured
    bool available()const noexcept{return configuration.has_value();}
};
inline constexpr qsizetype NativeRuntimeConfigurationLimit=16384;
// Recovery-1 andexplicit native transport-2. Their absent port means exactly22.
// Transport-2 carries a remote runtime directory andshared physical GPU lease
// directory, plus optional explicit host-key alias. None is remotely supplied.
// Port fields and implicit ssh_config alias/proxy lookup are refused. Decoding
// performs no command, connection or admission and creates no runtime paths.
// SSH executable is an explicit absolute caller argument; factory validates
// file metadata only. Key contents are NEVER opened/read. Local paths are
// syntactically checked but NOT created/initialized, absence is not readiness.
// Host form is direct hostname/IPv4, not host:port or
// an undefined IPv6/URL grammar. Future endpoint schemas require explicit work.
NativeRuntimeConfigurationResult decodeNativeRuntimeConfiguration(
    const QByteArray&,const QString& sshExecutable,const NativeRuntimePaths&,const QString& sourcePath={}) noexcept;
NativeRuntimeConfigurationResult loadNativeRuntimeConfiguration(
    const QString& configFile,const QString& sshExecutable,const NativeRuntimePaths&) noexcept;
}
