#pragma once
#include <QStringList>
class QWidget;
namespace pgn::ui {
void bindVerificationReceipts(QWidget*,const QString& journal,const QStringList& qualifiedValidators);
}
