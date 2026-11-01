// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendcoinsentry.h>
#include <qt/pgntransactionamount.h>
#include <QSignalBlocker>
#include <QThread>
#include <qt/forms/ui_sendcoinsentry.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <qt/pgnwalletamountbinding.h>

#include <QApplication>
#include <QClipboard>

SendCoinsEntry::SendCoinsEntry(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SendCoinsEntry),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);
    m_amount_binding = new WalletTransferAmountBinding(*ui->payAmount);
    ui->unitlLabel->setTextFormat(Qt::PlainText);
    connect(m_amount_binding, &WalletTransferAmountBinding::stateChanged, this, [this] {
        ui->useAvailableBalanceButton->setEnabled(false);
        ui->unitlLabel->setText(ui->payAmount->scaleText());
    });
    ui->unitlLabel->setText(ui->payAmount->scaleText());

    ui->addressBookButton->setIcon(platformStyle->SingleColorIcon(":/icons/address-book"));
    ui->pasteButton->setIcon(platformStyle->SingleColorIcon(":/icons/editpaste"));
    ui->deleteButton->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));

    if (platformStyle->getUseExtraSpacing())
        ui->payToLayout->setSpacing(4);

    GUIUtil::setupAddressWidget(ui->payTo, this);

    // Connect signals
    connect(ui->payAmount, &RiecoinAmountField::valueChanged, this, &SendCoinsEntry::payAmountChanged);
    connect(ui->checkboxSubtractFeeFromAmount, &QCheckBox::toggled, this, &SendCoinsEntry::subtractFeeFromAmountChanged);
    connect(ui->deleteButton, &QPushButton::clicked, this, &SendCoinsEntry::deleteClicked);
    connect(ui->useAvailableBalanceButton, &QPushButton::clicked, this, &SendCoinsEntry::useAvailableBalanceClicked);
    ui->useAvailableBalanceButton->setEnabled(false);
    ui->useAvailableBalanceButton->setToolTip(tr("Unavailable: exact spendable balance requires coin-control, locks and avoid-reuse context."));
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    updateLabel(address);
    // Dust/rate admission is not an input precision/scale policy. Its real
    // transaction-construction contract remains separate, not inherited here.
}

void SendCoinsEntry::setModel(WalletModel *_model)
{
    ui->useAvailableBalanceButton->setEnabled(false);
    this->model = _model;
    m_amount_binding->setWalletModel(_model);
    clear();
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    if (model && model->getOptionsModel()) {
        ui->checkboxSubtractFeeFromAmount->setChecked(model->getOptionsModel()->getSubFeeFromAmount());
    }
    ui->messageTextLabel->clear();
    ui->messageTextLabel->hide();
    ui->messageLabel->hide();
}

void SendCoinsEntry::checkSubtractFeeFromAmount()
{
    ui->checkboxSubtractFeeFromAmount->setChecked(true);
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

void SendCoinsEntry::useAvailableBalanceClicked()
{
    Q_EMIT useAvailableBalance(this);
}

bool SendCoinsEntry::validate(interfaces::Node& node)
{
    if (!model || !m_amount_binding->refresh())
        return false;

    // Check input validity
    bool retval = true;

    if (!model->validateAddress(ui->payTo->text())) {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (!ui->payAmount->validate())
        retval = false;

    return retval;
}

std::optional<SendCoinsRecipient> SendCoinsEntry::getValue()
{
    const QPointer<SendCoinsEntry> alive(this);
    if (!m_amount_binding->refresh() || !alive) return std::nullopt;
    const auto amount = ui->payAmount->value();
    if (!amount) return std::nullopt;
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    recipient.amount = *amount;
    recipient.message = ui->messageTextLabel->text();
    recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    QWidget::setTabOrder(ui->deleteButton, ui->addAsLabel);
    QWidget::setTabOrder(ui->addAsLabel, ui->payAmount);
    QWidget::setTabOrder(ui->payAmount, ui->checkboxSubtractFeeFromAmount);
    return ui->checkboxSubtractFeeFromAmount;
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    recipient = value;
    {
        // message
        ui->messageTextLabel->setText(recipient.message);
        ui->messageTextLabel->setVisible(!recipient.message.isEmpty());
        ui->messageLabel->setVisible(!recipient.message.isEmpty());

        ui->addAsLabel->clear();
        ui->payTo->setText(recipient.address); // this may set a label from addressbook
        if (!recipient.label.isEmpty()) // if a label had been set from the addressbook, don't overwrite with an empty label
            ui->addAsLabel->setText(recipient.label);
        ui->payAmount->setValue(recipient.amount);
    }
}

void SendCoinsEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

void SendCoinsEntry::setAmount(const CAmount &amount)
{
    ui->payAmount->setValue(amount);
}

std::optional<pgn::core::MonetaryContext> SendCoinsEntry::amountContext() const
{
    return ui->payAmount->transferContext();
}

QString SendCoinsEntry::amountInputText() const
{
    return ui->payAmount->text();
}

void SendCoinsEntry::setUseAvailableContext(std::optional<pgn::core::MonetaryContext> context)
{
    if (QThread::currentThread() != thread()) return;
    const bool available = model && context &&
        pgn::qt::SameTransactionMonetaryContext(context, amountContext());
    ui->useAvailableBalanceButton->setEnabled(available);
    ui->useAvailableBalanceButton->setToolTip(available
        ? tr("Query native coin-control candidates again. This is not a fee quote or authorization to spend.")
        : tr("Unavailable: native coin-control candidate provider or denomination context."));
}

bool SendCoinsEntry::applyAvailableAmount(WalletModel& owner, const pgn::core::MonetaryContext& context, CAmount amount)
{
    if (QThread::currentThread() != thread() || model.data() != &owner ||
        !pgn::qt::SameTransactionMonetaryContext(amountContext(), context) || amount <= 0) return false;
    {
        const QSignalBlocker amount_blocked(ui->payAmount);
        const QSignalBlocker fee_blocked(ui->checkboxSubtractFeeFromAmount);
        if (!ui->payAmount->trySetValuePreservingInput(amount, context)) return false;
        // Upstream behavior: a gross remainder, not an estimated net fee quote.
        ui->checkboxSubtractFeeFromAmount->setChecked(true);
        if (ui->payAmount->value() != std::optional<CAmount>{amount}) return false;
    }
    const QPointer<SendCoinsEntry> alive(this);
    Q_EMIT payAmountChanged();
    if (alive) Q_EMIT subtractFeeFromAmountChanged();
    return true;
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        ui->addressBookButton->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/address-book")));
        ui->pasteButton->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/editpaste")));
        ui->deleteButton->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/remove")));
    }

    QWidget::changeEvent(e);
}

bool SendCoinsEntry::updateLabel(const QString &address)
{
    if(!model || address.isEmpty())
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}
