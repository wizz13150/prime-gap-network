#pragma once
#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QWidget>
#include <functional>
#include <vector>
namespace pgn::wallet {class CoreRuntime;}
namespace pgn::ui {
// A single bounded in-memory snapshot serves both wallet overview and history.
class NetworkMetricsHub final:public QObject {
public:
    using Reader=std::function<void(int,std::function<void(QJsonObject,QString)>)>;
    explicit NetworkMetricsHub(Reader,QObject* parent=nullptr);
    void subscribe(QWidget*,std::function<void(const QJsonObject&,const QString&)>);
    void refresh();
    void setSpan(int);
    int span()const{return span_;}
private:
    bool eventFilter(QObject*,QEvent*) override;
    struct Subscriber {QPointer<QWidget> owner;std::function<void(const QJsonObject&,const QString&)> receive;};
    Reader reader_;QTimer timer_;QJsonObject snapshot_;QString error_;std::vector<Subscriber> subscribers_;
    QElapsedTimer lastDispatch_;
    int span_{86400};bool busy_{},pending_{};
};
QWidget* createNetworkMetricsView(NetworkMetricsHub*,bool compact=false);
}
