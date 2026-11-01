#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <memory>
#include <optional>

namespace pgn::mining {
// Compatibility with engine-module-registry.mjs's sorted-key canonical():
// JS binary64 numbers (NOT exact PGN amounts/counters), UTF16 key ordering,
// JSON.stringify strings including escaped lone surrogates. No Qt JSON hash.
QByteArray canonicalEngineModuleJson(const QJsonValue&);
QJsonObject validateEngineModuleCapabilities(const QJsonValue&);
QJsonObject observeEngineModule(const QJsonValue&,const QJsonObject& provenance={});
QJsonObject createEngineModuleRequest(const QJsonObject& observation,const QJsonObject& intent);

// SAME existing engine-modules.sqlite tables/schema. Lazy open: page() on an
// absent file creates NOTHING. Existing legacy artifact_ref migration matches
// Node; never rewrites old keys/ages/unknown extensions. Creating-thread only,
// called by the application's ONE writer, not a second executor/writer.
class EngineModuleRegistry final {
public:
    explicit EngineModuleRegistry(QString database);
    ~EngineModuleRegistry();
    EngineModuleRegistry(const EngineModuleRegistry&)=delete;
    EngineModuleRegistry& operator=(const EngineModuleRegistry&)=delete;
    QJsonObject registerModule(const QJsonValue& package,const QString& sourceRef="User supplied descriptor");
    QJsonObject page(const QString& after={},int size=32);
    // Unsupported/corrupt JSON stays stored: capability=null, explicit reason,
    // immutable metadata plus opaque capabilities_json; never a fake protocol.
    QJsonObject descriptor(const QString& moduleKey);
    QJsonObject request(const QJsonObject& intent);
    QJsonObject receipt(const QString& requestId);
    void close();
private:
    class Impl;std::unique_ptr<Impl> d_;
};
// Four ABI actions ONLY. Metadata/requested intent, applied=false, zero GPU
// calls/reservations. Register/select must be serialized as writer Mutation;
// list/receipt as Read. This handler does not admit/observe/execute an engine.
QJsonObject handleModuleRegistryCommand(EngineModuleRegistry&,const QJsonObject&);
}
