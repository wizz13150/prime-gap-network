#include "ui/wallet_core_view.h"
#include <QApplication>
#include <QLabel>
#include <QTemporaryDir>
#include <QFileInfo>
#include <memory>
#include <iostream>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    for (bool testnet : {false, true}) {
        QTemporaryDir root;
        if (!root.isValid()) return 1;
        std::unique_ptr<QWidget> view(pgn::ui::createWalletCoreView(root.path(), {}, testnet));
        view->resize(1380, 800);
        view->show();
        app.processEvents();
        auto* account = view->findChild<QWidget*>("pgn-wallet-account");
        if (!account || account->isEnabled()) return 2;
        const QString unit = testnet ? "tPGN" : "PGN";
        const QString other = testnet ? "PGN" : "tPGN";
        int unknownBalances = 0;
        for (auto* label : account->findChildren<QLabel*>()) {
            unknownBalances += label->text() == QString::fromUtf8("— ") + unit;
            if (label->text() == QString::fromUtf8("— ") + other) return 3;
        }
        if (unknownBalances != 3) return 4;
        if (QFileInfo::exists(root.path()+"/data/runtime/wallet-owned.json") ||
            QFileInfo::exists(root.path()+"/data/testnet/ui-runtime/wallet-owned.json")) return 5;
    }
    std::cout << "PASS: actual wallet widgets select PGN/mainnet and tPGN/testnet; "
                 "three unavailable balances stay unknown, no Core or wallet is created\n";
    return 0;
}
