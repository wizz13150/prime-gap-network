// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_ASKPASSPHRASEDIALOG_H
#define BITCOIN_QT_ASKPASSPHRASEDIALOG_H

#include <QDialog>
#include <QPointer>

#include <support/allocators/secure.h>
#include <interfaces/walletunlock.h>

class WalletModel;

namespace Ui {
    class AskPassphraseDialog;
}

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class AskPassphraseDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        Encrypt,    /**< Ask passphrase twice and encrypt */
        Unlock,     /**< Ask passphrase and unlock */
        ChangePass, /**< Ask old passphrase + new passphrase twice */
    };

    explicit AskPassphraseDialog(Mode mode, QWidget *parent, SecureString* passphrase_out = nullptr);
    ~AskPassphraseDialog();

    void accept() override;

    void setModel(WalletModel *model);
    void setUnlockFunction(interfaces::WalletUnlockFn unlock) { m_unlock = std::move(unlock); }

private:
    Ui::AskPassphraseDialog *ui;
    Mode mode;
    QPointer<WalletModel> model;
    QMetaObject::Connection m_model_destroyed;
    bool fCapsLock{false};
    SecureString* m_passphrase_out;
    interfaces::WalletUnlockFn m_unlock;

private Q_SLOTS:
    void textChanged();
    void secureClearPassFields();
    void toggleShowPassword(bool);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
};

#endif // BITCOIN_QT_ASKPASSPHRASEDIALOG_H
