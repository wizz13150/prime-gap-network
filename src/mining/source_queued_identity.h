#pragma once
#include <QJsonObject>
#include <optional>
#include <stdexcept>

namespace pgn::mining::source_queued_identity {
// Legacy SOURCE receipts may have no local booking. A PRESENT malformed
// identity is never silently converted to that unqueued case.
inline std::optional<QString> queued(const QJsonObject& object){
 const auto value=object.value("queued_intent");
 if(value.isUndefined()||value.isNull())return {};
 if(!value.isString()||value.toString().isEmpty()||value.toString().size()>128||
    value.toString().contains(QChar(0))||QString::fromUtf8(value.toString().toUtf8())!=value.toString())
  throw std::runtime_error("Queued source identity is not exact text");
 return value.toString();
}
}
