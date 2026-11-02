#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <iostream>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace pgn::test {
// Verify the SDK's actual offscreen plugin before QApplication can select a
// desktop backend. An explicit path remains supported by isolated runners.
inline bool prepareOffscreen(const QString& explicitPlugin = {}) {
#ifdef Q_OS_WIN
    const QString fileName = "qoffscreen.dll";
#elif defined(Q_OS_DARWIN)
    const QString fileName = "libqoffscreen.dylib";
#else
    const QString fileName = "libqoffscreen.so";
#endif
    QStringList roots{QLibraryInfo::path(QLibraryInfo::PluginsPath)};
    roots.append(QCoreApplication::libraryPaths());
#ifdef Q_OS_WIN
    wchar_t image[32768];
    const auto module = GetModuleHandleW(L"Qt6Core.dll");
    const auto length = module ? GetModuleFileNameW(module, image, 32768) : 0;
    if (length && length < 32768) {
        const auto core = QFileInfo(QString::fromWCharArray(image, int(length))).absolutePath();
        roots.prepend(core);
        roots.append(QDir::cleanPath(core + "/../share/qt6/plugins"));
    }
#endif
    QStringList candidates;
    if (!explicitPlugin.isEmpty()) candidates.append(explicitPlugin);
    else for (const auto& root : roots) candidates.append(root + "/platforms/" + fileName);
    candidates.removeDuplicates();
    QStringList failures;
    for (const auto& candidate : candidates) {
        if (!QFileInfo(candidate).isFile()) continue;
        QPluginLoader plugin(candidate);
        if (!plugin.metaData().value("MetaData").toObject().value("Keys").toArray().contains("offscreen") || !plugin.load()) {
            failures.append(candidate + ": " + plugin.errorString());
            continue;
        }
        const auto platforms = QFileInfo(candidate).absolutePath();
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platforms.toUtf8());
        QCoreApplication::addLibraryPath(QFileInfo(platforms).absolutePath());
        std::cout << "OFFSCREEN PREFLIGHT plugin=" << candidate.toStdString() << '\n';
        return true;
    }
    std::cerr << "OFFSCREEN PREFLIGHT FAILED: " << candidates.join("; ").toStdString()
              << " " << failures.join("; ").toStdString() << '\n';
    return false;
}
}
