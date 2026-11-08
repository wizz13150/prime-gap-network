#pragma once
#include "update/component_update_service.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <functional>
class QLabel;class QPushButton;class QTableWidget;class QTimer;class QProgressBar;
namespace pgn::ui {
struct ComponentVersionsContext {
 QMap<QString,QString> codeRoots;
 QMap<QString,QString> primaryPrograms;
 QString manifestPath,applicationRoot,mutableDataRoot,genesisHash;
 bool testnet{},fetchOnOpen{true};
};
ComponentVersionsContext walletComponentVersionsContext(const QString& productRoot,const QString& runtimeDirectory,bool testnet);
QJsonObject inspectLocalComponentVersions(const ComponentVersionsContext&);
class ComponentVersionsDialog final:public QDialog {
public:
 explicit ComponentVersionsDialog(ComponentVersionsContext,QWidget* parent=nullptr);
 ~ComponentVersionsDialog()override;
 void refreshLocal();
 void refreshPublished();
 void presentLocal(const QJsonObject&);
 void presentPublished(const QJsonObject&,const QString& status={});
 void presentProgress(const QJsonObject&);
 void presentUpdateFailure(const QString&);
 std::function<void(const QJsonObject&)> onLocalSnapshot;
 std::function<void(const QJsonObject&)> onPublishedCatalog;
 std::function<void(const QString&)> onPublishedFailure;
private:
 void updateRows();void prepareSelected();void installPrepared();void setBusy(bool);void updateRecovery();
 QStringList selected()const;QJsonObject request(const QString&,const QStringList&)const;bool ensureService();
 ComponentVersionsContext context_;QLabel* localSummary_{};QLabel* status_{};QTableWidget* components_{};
 QProgressBar* progress_{};
 QPushButton *refresh_{},*selectAll_{},*prepare_{},*install_{},*cancel_{},*recover_{};
 QPointer<QFutureWatcher<QJsonObject>> localWatcher_;
 pgn::update::ComponentUpdateService* service_{};pgn::update::ComponentUpdateDeployment deployment_;
 QJsonObject localSnapshot_,published_,preparedRequest_;QTimer* resultPoll_{};
 bool localBusy_{},busy_{},closing_{},installStarted_{},publishedFresh_{};
};
}
