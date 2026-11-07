#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <memory>

namespace pgn::wallet {
// Public address metadata only. Private keys stay in protected key sessions.
// The database is capped at16MiB; the conservative session budget is128MiB.
class VanityResultStore {
public:
    explicit VanityResultStore(const QString& directory,qint64 budget=128*1024*1024);
    ~VanityResultStore();
    VanityResultStore(const VanityResultStore&)=delete;
    VanityResultStore& operator=(const VanityResultStore&)=delete;
    QString append(const QJsonObject&,qint64 protectedBytes);
    QJsonArray page(int offset,int limit=1000)const;
    qint64 count()const;
    qint64 reservedBytes()const;
    QString sessionFor(const QString& address,const QString& network)const;
    const QString& error()const;
    const QString& pageError()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
