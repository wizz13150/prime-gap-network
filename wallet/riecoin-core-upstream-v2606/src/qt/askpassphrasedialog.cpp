// Copyright (c) 2011-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/askpassphrasedialog.h>
#include <qt/forms/ui_askpassphrasedialog.h>

#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <support/allocators/secure.h>

#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>

AskPassphraseDialog::AskPassphraseDialog(Mode _mode, QWidget *parent, SecureString* passphrase_out) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::AskPassphraseDialog),
    mode(_mode),
    m_passphrase_out(passphrase_out)
{
    ui->setupUi(this);

    ui->passEdit1->setMinimumSize(ui->passEdit1->sizeHint());
    ui->passEdit2->setMinimumSize(ui->passEdit2->sizeHint());
    ui->passEdit3->setMinimumSize(ui->passEdit3->sizeHint());

    ui->passEdit1->setMaxLength(MAX_PASSPHRASE_SIZE);
    ui->passEdit2->setMaxLength(MAX_PASSPHRASE_SIZE);
    ui->passEdit3->setMaxLength(MAX_PASSPHRASE_SIZE);

    // Setup Caps Lock detection.
    ui->passEdit1->installEventFilter(this);
    ui->passEdit2->installEventFilter(this);
    ui->passEdit3->installEventFilter(this);

    switch(mode)
    {
        case Encrypt: // Ask passphrase x2
            ui->warningLabel->setText(tr("Enter the new passphrase for the wallet.<br/>Please use a passphrase of <b>ten or more random characters</b>, or <b>eight or more words</b>."));
            ui->passLabel1->hide();
            ui->passEdit1->hide();
            setWindowTitle(tr("Encrypt wallet"));
            break;
        case Unlock: // Ask passphrase
            ui->warningLabel->setText(tr("This operation needs your wallet passphrase to unlock the wallet."));
            ui->passLabel2->hide();
            ui->passEdit2->hide();
            ui->passLabel3->hide();
            ui->passEdit3->hide();
            setWindowTitle(tr("Unlock wallet"));
            break;
        case ChangePass: // Ask old passphrase + new passphrase x2
            setWindowTitle(tr("Change passphrase"));
            ui->warningLabel->setText(tr("Enter the old passphrase and new passphrase for the wallet."));
            break;
    }
    textChanged();
    connect(ui->toggleShowPasswordButton, &QPushButton::toggled, this, &AskPassphraseDialog::toggleShowPassword);
    connect(ui->passEdit1, &QLineEdit::textChanged, this, &AskPassphraseDialog::textChanged);
    connect(ui->passEdit2, &QLineEdit::textChanged, this, &AskPassphraseDialog::textChanged);
    connect(ui->passEdit3, &QLineEdit::textChanged, this, &AskPassphraseDialog::textChanged);

    GUIUtil::handleCloseWindowShortcut(this);
}

AskPassphraseDialog::~AskPassphraseDialog()
{
    secureClearPassFields();
    delete ui;
}

void AskPassphraseDialog::setModel(WalletModel *_model)
{
    if (thread()!=QThread::currentThread()) return;
    disconnect(m_model_destroyed);
    this->model = _model && _model->thread()==thread() ? _model : nullptr;
    if (model) m_model_destroyed=connect(model.data(),&QObject::destroyed,this,&QDialog::reject,Qt::DirectConnection);
    else if (_model) reject();
}

void AskPassphraseDialog::accept()
{
    const QPointer<AskPassphraseDialog> alive(this);
    const QPointer<WalletModel> owner(model);
    const auto current_owner=[&] {
        return alive && owner && model==owner && thread()==QThread::currentThread() && owner->thread()==QThread::currentThread();
    };
    if (mode==Unlock && !current_owner()) return;
    SecureString oldpass, newpass1, newpass2;
    if (!model && mode != Encrypt)
        return;
    oldpass.reserve(MAX_PASSPHRASE_SIZE);
    newpass1.reserve(MAX_PASSPHRASE_SIZE);
    newpass2.reserve(MAX_PASSPHRASE_SIZE);

    oldpass.assign(std::string_view{ui->passEdit1->text().toStdString()});
    newpass1.assign(std::string_view{ui->passEdit2->text().toStdString()});
    newpass2.assign(std::string_view{ui->passEdit3->text().toStdString()});

    secureClearPassFields();
    if (!alive) return;
    textChanged();
    if (!alive || (mode==Unlock && !current_owner())) return;

    switch(mode)
    {
    case Encrypt: {
        if(newpass1.empty() || newpass2.empty())
        {
            // Cannot encrypt with empty passphrase
            break;
        }
        QMessageBox msgBoxConfirm(QMessageBox::Question,
                                  tr("Confirm wallet encryption"),
                                  tr("Warning: If you encrypt your wallet and lose your passphrase, you will <b>LOSE ALL OF YOUR RIECOINS</b>!") + "<br><br>" + tr("Are you sure you wish to encrypt your wallet?"),
                                  QMessageBox::Cancel | QMessageBox::Yes, this);
        msgBoxConfirm.button(QMessageBox::Yes)->setText(tr("Continue"));
        msgBoxConfirm.button(QMessageBox::Cancel)->setText(tr("Back"));
        msgBoxConfirm.setDefaultButton(QMessageBox::Cancel);
        QMessageBox::StandardButton retval = (QMessageBox::StandardButton)msgBoxConfirm.exec();
        if(retval == QMessageBox::Yes)
        {
            if(newpass1 == newpass2)
            {
                QString encryption_reminder = tr("Remember that encrypting your wallet cannot fully protect "
                "your Riecoins from being stolen by malware infecting your computer.");
                if (m_passphrase_out) {
                    m_passphrase_out->assign(newpass1);
                    QMessageBox msgBoxWarning(QMessageBox::Warning,
                                              tr("Wallet to be encrypted"),
                                              "<qt>" +
                                                  tr("Your wallet is about to be encrypted. ") + encryption_reminder + " " +
                                                  tr("Are you sure you wish to encrypt your wallet?") +
                                                  "</b></qt>",
                                              QMessageBox::Cancel | QMessageBox::Yes, this);
                    msgBoxWarning.setDefaultButton(QMessageBox::Cancel);
                    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)msgBoxWarning.exec();
                    if (retval == QMessageBox::Cancel) {
                        QDialog::reject();
                        return;
                    }
                } else {
                    assert(model != nullptr);
                    if (model->setWalletEncrypted(newpass1)) {
                        QMessageBox::warning(this, tr("Wallet encrypted"),
                                             "<qt>" +
                                             tr("Your wallet is now encrypted. ") + encryption_reminder +
                                             "<br><br><b>" +
                                             tr("IMPORTANT: Any previous backups you have made of your wallet file "
                                             "should be replaced with the newly generated, encrypted wallet file. "
                                             "For security reasons, previous backups of the unencrypted wallet file "
                                             "will become useless as soon as you start using the new, encrypted wallet.") +
                                             "</b></qt>");
                    } else {
                        QMessageBox::critical(this, tr("Wallet encryption failed"),
                                             tr("Wallet encryption failed due to an internal error. Your wallet was not encrypted."));
                    }
                }
                QDialog::accept(); // Success
            }
            else
            {
                QMessageBox::critical(this, tr("Wallet encryption failed"),
                                     tr("The supplied passphrases do not match."));
            }
        }
    } break;
    case Unlock:
        try {
            if (!current_owner()) return;
            // Keep the native callable alive even if its status notification
            // deletes this dialog. Manual/menu unlock retains the stock path.
            const auto unlock = m_unlock;
            const bool unlocked = unlock ? unlock(oldpass) : owner->setWalletLocked(false, oldpass);
            if (!current_owner()) return;
            if (!unlocked) {
                // Check if the passphrase has a null character (see #27067 for details)
                if (oldpass.find('\0') == std::string::npos) {
                    QMessageBox::critical(this, tr("Wallet unlock failed"),
                                          tr("The passphrase entered for the wallet decryption was incorrect."));
                } else {
                    QMessageBox::critical(this, tr("Wallet unlock failed"),
                                          tr("The passphrase entered for the wallet decryption is incorrect. "
                                             "It contains a null character (ie - a zero byte). "
                                             "If the passphrase was set with a version of this software prior to 25.0, "
                                             "please try again with only the characters up to — but not including — "
                                             "the first null character. If this is successful, please set a new "
                                             "passphrase to avoid this issue in the future."));
                }
            } else {
                if (m_passphrase_out) {
                    m_passphrase_out->assign(oldpass);
                }
                QDialog::accept(); // Success
            }
        } catch (const std::runtime_error& e) {
            if (current_owner()) QMessageBox::critical(this, tr("Wallet unlock failed"), QString::fromUtf8(e.what()).toHtmlEscaped());
        }
        break;
    case ChangePass:
        if(newpass1 == newpass2)
        {
            if(model->changePassphrase(oldpass, newpass1))
            {
                QMessageBox::information(this, tr("Wallet encrypted"),
                                     tr("Wallet passphrase was successfully changed."));
                QDialog::accept(); // Success
            }
            else
            {
                // Check if the old passphrase had a null character (see #27067 for details)
                if (oldpass.find('\0') == std::string::npos) {
                    QMessageBox::critical(this, tr("Passphrase change failed"),
                                          tr("The passphrase entered for the wallet decryption was incorrect."));
                } else {
                    QMessageBox::critical(this, tr("Passphrase change failed"),
                                          tr("The old passphrase entered for the wallet decryption is incorrect. "
                                             "It contains a null character (ie - a zero byte). "
                                             "If the passphrase was set with a version of this software prior to 25.0, "
                                             "please try again with only the characters up to — but not including — "
                                             "the first null character."));
                }
            }
        }
        else
        {
            QMessageBox::critical(this, tr("Wallet encryption failed"),
                                 tr("The supplied passphrases do not match."));
        }
        break;
    }
}

void AskPassphraseDialog::textChanged()
{
    // Validate input, set Ok button to enabled when acceptable
    bool acceptable = false;
    switch(mode)
    {
    case Encrypt: // New passphrase x2
        acceptable = !ui->passEdit2->text().isEmpty() && !ui->passEdit3->text().isEmpty();
        break;
    case Unlock: // Old passphrase x1
        acceptable = !ui->passEdit1->text().isEmpty();
        break;
    case ChangePass: // Old passphrase x1, new passphrase x2
        acceptable = !ui->passEdit1->text().isEmpty() && !ui->passEdit2->text().isEmpty() && !ui->passEdit3->text().isEmpty();
        break;
    }
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(acceptable);
}

bool AskPassphraseDialog::event(QEvent *event)
{
    // Detect Caps Lock key press.
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_CapsLock) {
            fCapsLock = !fCapsLock;
        }
        if (fCapsLock) {
            ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
        } else {
            ui->capsLabel->clear();
        }
    }
    return QWidget::event(event);
}

void AskPassphraseDialog::toggleShowPassword(bool show)
{
    ui->toggleShowPasswordButton->setDown(show);
    const auto mode = show ? QLineEdit::Normal : QLineEdit::Password;
    ui->passEdit1->setEchoMode(mode);
    ui->passEdit2->setEchoMode(mode);
    ui->passEdit3->setEchoMode(mode);
}

bool AskPassphraseDialog::eventFilter(QObject *object, QEvent *event)
{
    /* Detect Caps Lock.
     * There is no good OS-independent way to check a key state in Qt, but we
     * can detect Caps Lock by checking for the following condition:
     * Shift key is down and the result is a lower case character, or
     * Shift key is not down and the result is an upper case character.
     */
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        QString str = ke->text();
        if (str.length() != 0) {
            const QChar *psz = str.unicode();
            bool fShift = (ke->modifiers() & Qt::ShiftModifier) != 0;
            if ((fShift && *psz >= 'a' && *psz <= 'z') || (!fShift && *psz >= 'A' && *psz <= 'Z')) {
                fCapsLock = true;
                ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
            } else if (psz->isLetter()) {
                fCapsLock = false;
                ui->capsLabel->clear();
            }
        }
    }
    return QDialog::eventFilter(object, event);
}

static void SecureClearQLineEdit(QLineEdit* edit)
{
    if (!edit) return;
    const QPointer<QLineEdit> alive(edit);
    const bool blocked=edit->blockSignals(true);
    // Attempt to overwrite text so that they do not linger around in memory
    edit->setText(QString(" ").repeated(edit->text().size()));
    if (!alive) return;
    alive->clear();
    if (alive) alive->blockSignals(blocked); // No blocker destructor can touch a dead QObject.
}

void AskPassphraseDialog::secureClearPassFields()
{
    const QPointer<QLineEdit> first(ui->passEdit1),second(ui->passEdit2),third(ui->passEdit3);
    SecureClearQLineEdit(first.data());
    SecureClearQLineEdit(second.data());
    SecureClearQLineEdit(third.data());
}
