#pragma once
#include <QObject>
#include <QJsonObject>
#include <functional>
#include <memory>

namespace pgn::science {
// Parses the published data dump as data only. No SQL or downloaded expression
// is executed. Unsupported/truncated input cannot replace a valid reference.
QJsonObject parseRecordReferenceSql(const QByteArray&,const QString& retrievedUtc);
class RecordReferenceRefresh final:public QObject {
public:
    explicit RecordReferenceRefresh(QString file,QObject* parent=nullptr);
    ~RecordReferenceRefresh() override;
    void refresh(bool force=false);
    std::function<void(bool,const QString&)> onChanged;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
