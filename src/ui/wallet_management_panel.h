#pragma once
#include "wallet/wallet_manager.h"
#include <QWidget>
#include <functional>
class QLabel;class QComboBox;class QPushButton;class QToolButton;
namespace pgn::ui {
class WalletManagementPanel final:public QWidget {
public:
 explicit WalletManagementPanel(pgn::wallet::WalletManager*,QWidget* parent=nullptr);
 void present(const QString& network,const QString& dataDirectory,bool configured,bool pending=false,const QString& pendingDirectory={});
 void refresh();
 std::function<void()> onConfigure;
 std::function<void()> onCancelChange;
 std::function<void()> onComponents;
private:
 pgn::wallet::WalletManager* manager_{};QLabel *location_{},*service_{},*message_{};
 QComboBox* wallets_{};QPushButton *configure_{},*cancelChange_{},*reload_{},*create_{},*load_{},*select_{},*close_{};QToolButton* open_{};
};
}
