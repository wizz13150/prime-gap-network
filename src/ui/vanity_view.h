#pragma once
#include <QString>
#include <QJsonObject>
#include <functional>
class QWidget;
namespace pgn::wallet {class CoreRuntime;}
namespace pgn::ui {
struct VanityGpuServices {
    QString device,label,host;
    QString sshProgram;
    QJsonObject remoteEndpoint;
    std::function<QString()> state;
    std::function<void(const QString&,std::function<void(QString)>)> pause;
    std::function<void(const QString&,bool,std::function<void(QString)>)> release;
};
QWidget* createVanityView(const QString& root,pgn::wallet::CoreRuntime* runtime=nullptr,const QString& vaultDirectory={},bool testnet=false);
void bindVanityGpu(QWidget* wallet,const VanityGpuServices&);
bool drainVanityForClose(QWidget* wallet,std::function<void()>);
}

