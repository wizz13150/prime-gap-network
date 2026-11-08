#pragma once
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <memory>
namespace pgn::ui {
class AuditAnimationView final:public QWidget{
public:
 using Reply=std::function<void(QJsonObject,QString)>;
 using Reader=std::function<void(qint64,const QString&,const QJsonArray&,Reply)>;
 using Projection=std::function<QJsonObject(const QJsonArray&,QWidget*)>;
 AuditAnimationView(Reader,Projection,QWidget* parent=nullptr);
 ~AuditAnimationView()override;
 void preview();
 void reproject();
protected:
 void showEvent(QShowEvent*)override;
 void hideEvent(QHideEvent*)override;
 void resizeEvent(QResizeEvent*)override;
private:
 struct Impl;std::unique_ptr<Impl> impl_;
 void refresh();
};
}
