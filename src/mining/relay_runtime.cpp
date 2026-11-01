#include "mining/relay_runtime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace pgn::mining {
namespace {
QString executableFile(const QString& path) {
    const QFileInfo file(path);
    if (!file.isFile() || !file.isExecutable()) return {};
    return file.canonicalFilePath();
}
QString nodeName() {
#ifdef Q_OS_WIN
    return QStringLiteral("node.exe");
#else
    return QStringLiteral("node");
#endif
}
}
RelayRuntime resolveRelayRuntime(const QString& root, const QString& appDir,
    const QString& explicitProgram, const QStringList& searchDirectories,
    const QString& home) {
    // An explicit operator configuration is never silently replaced by a
    // different program when its file is missing or invalid.
    if (!explicitProgram.isEmpty()) {
        if (!QFileInfo(explicitProgram).isAbsolute())
            return {{}, {}, "PGN_NODE_EXECUTABLE must be an absolute executable path"};
        const auto program = executableFile(explicitProgram);
        if (program.isEmpty())
            return {{}, {}, "Configured PGN_NODE_EXECUTABLE is unavailable; no relay started"};
        return {program, "explicit configuration", {}};
    }
    const QStringList bundled{
        QDir(root).filePath("runtime/node/" + nodeName()),
        QDir(appDir).filePath("runtime/node/" + nodeName())};
    for (const auto& path : bundled) {
        const auto program = executableFile(path);
        if (!program.isEmpty()) return {program, "product runtime", {}};
    }
    // Development fallback is derived from the actual profile, never a
    // particular user's name. Prefer it to an unrelated system Node version.
    if (!home.isEmpty()) {
        const auto program = executableFile(QDir(home).filePath(
            ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/" + nodeName()));
        if (!program.isEmpty()) return {program, "development runtime", {}};
    }
    for (const auto& directory : searchDirectories) {
        // Empty PATH segments must not admit the mutable working directory.
        if (directory.isEmpty() || !QFileInfo(directory).isAbsolute()) continue;
        const auto program = executableFile(QDir(directory).filePath(nodeName()));
        if (!program.isEmpty()) return {program, "system PATH", {}};
    }
    return {{}, {}, "Node runtime unavailable. Bundle runtime/node or set PGN_NODE_EXECUTABLE. No relay or mining command was started."};
}
RelayRuntime resolveRelayRuntime(const QString& root) {
    return resolveRelayRuntime(root, QCoreApplication::applicationDirPath(),
        qEnvironmentVariable("PGN_NODE_EXECUTABLE"),
        qEnvironmentVariable("PATH").split(QDir::listSeparator()), QDir::homePath());
}
}
