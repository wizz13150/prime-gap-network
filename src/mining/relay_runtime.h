#pragma once
#include <QString>
#include <QStringList>

namespace pgn::mining {
// This selects a transport dependency only. It cannot start a GPU, a wallet,
// a network node or change any durable mining ownership.
struct RelayRuntime {
    QString program;
    QString source;
    QString error;
    bool valid() const { return !program.isEmpty() && error.isEmpty(); }
};

RelayRuntime resolveRelayRuntime(const QString& productRoot,
    const QString& applicationDirectory, const QString& explicitProgram,
    const QStringList& searchDirectories, const QString& homeDirectory);
RelayRuntime resolveRelayRuntime(const QString& productRoot);
}
