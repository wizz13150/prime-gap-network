#include "mining/relay_runtime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const auto root = temporary.path() + "/product";
    const auto home = temporary.path() + "/different-profile";
    const auto appDir = temporary.path() + "/app";
    const auto pathDir = temporary.path() + "/system node";
#ifdef Q_OS_WIN
    const QString name = "node.exe";
#else
    const QString name = "node";
#endif
    auto create = [](const QString& path) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.close();
        return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::ExeOther);
    };
    auto resolve = [&](const QString& explicitProgram = {}) {
        return pgn::mining::resolveRelayRuntime(root, appDir, explicitProgram,
            {"", "relative", pathDir}, home);
    };
    if (resolve().valid()) return 2;
    const auto system = pathDir + "/" + name;
    if (!create(system) || resolve().program != QFileInfo(system).canonicalFilePath()) return 3;
    const auto development = home + "/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/" + name;
    if (!create(development) || resolve().program != QFileInfo(development).canonicalFilePath()) return 4;
    const auto packaged = root + "/runtime/node/" + name;
    if (!create(packaged) || resolve().program != QFileInfo(packaged).canonicalFilePath()) return 5;
    if (resolve(system).program != QFileInfo(system).canonicalFilePath()) return 6;
    if (resolve(root + "/missing.exe").valid() || resolve("relative.exe").valid() || resolve(root).valid()) return 7;
    // A relocated app also works without the product-root bundle/profile/PATH.
    const auto appRuntime = appDir + "/runtime/node/" + name;
    if (!create(appRuntime)) return 8;
    const auto relocated = pgn::mining::resolveRelayRuntime(temporary.path() + "/other", appDir, {}, {}, {});
    if (relocated.program != QFileInfo(appRuntime).canonicalFilePath()) return 9;
    std::cout << "PASS explicit, bundled, relocated, profile and PATH resolution; invalid overrides fail closed\n";
}
