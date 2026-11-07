#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <mutex>
#include <optional>

namespace pgn::app {
struct MutablePaths {QString codeRoot,dataRoot;};
inline std::mutex mutablePathsMutex;
inline std::optional<MutablePaths> mutablePaths;
inline QString cleanApplicationRoot(const QString& root){return QDir::cleanPath(QFileInfo(root).absoluteFilePath());}
// Pure resolution: inspecting/cancelling setup does not create a directory.
inline QString resolvedMutableDataRoot(const QString& codeRoot,const QString& genesis,bool explicitLegacyRoot){
 if(explicitLegacyRoot)return QDir(cleanApplicationRoot(codeRoot)).filePath("data");
 const auto base=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
 if(base.isEmpty()||!QFileInfo(base).isAbsolute())return {};
 const auto scope=QRegularExpression("^[0-9a-f]{64}$").match(genesis).hasMatch()?genesis:QString("unconfigured");
 return QDir(base).filePath("data/"+scope);
}
inline bool bindMutableDataRoot(const QString& codeRoot,const QString& genesis,bool explicitLegacyRoot){
 auto* app=QCoreApplication::instance();if(!app)return false;
 const auto data=resolvedMutableDataRoot(codeRoot,genesis,explicitLegacyRoot);if(data.isEmpty())return false;
 const auto code=cleanApplicationRoot(codeRoot);std::scoped_lock lock(mutablePathsMutex);
 if(mutablePaths)return mutablePaths->codeRoot==code&&mutablePaths->dataRoot==data;
 mutablePaths=MutablePaths{code,data};return true;
}
inline QString mutableDataRoot(const QString& root){
 const auto code=cleanApplicationRoot(root);std::scoped_lock lock(mutablePathsMutex);
 if(mutablePaths&&mutablePaths->codeRoot==code)return mutablePaths->dataRoot;
 // Keep explicit existing library/fixture callers compatible. The application
 // binds its single root before constructing any mutable production consumer.
 return QDir(cleanApplicationRoot(root)).filePath("data");
}
}
