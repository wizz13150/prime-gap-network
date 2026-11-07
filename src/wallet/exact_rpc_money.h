#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <optional>
namespace pgn::wallet {
struct ExactRpcMoney {QJsonValue value;QString error;};
ExactRpcMoney annotateExactRpcMoney(const QByteArray&,const QString& id,const QString& method,const QJsonValue&);
std::optional<qint64> parsePgnTransfer(const QString&);
std::optional<qint64> exactAtomicField(const QJsonObject&,const QString&);
QString formatAtomicPgn(qint64,bool compact=false);
std::optional<qint64> exactTransactionOutput(const QJsonObject&);
}
