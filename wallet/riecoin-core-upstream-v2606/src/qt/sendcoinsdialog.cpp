// Copyright (c) 2011-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <riecoin-build-config.h> // IWYU pragma: keep

#include <qt/sendcoinsdialog.h>
#include <qt/forms/ui_sendcoinsdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/pgncoincontrollabels.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/pgntransactionamount.h>
#include <qt/pgnsendamountdisplay.h>
#include <qt/pgncurrencyfeeratefield.h>
#include <qt/sendcoinsentry.h>
#include <qt/transactiontablemodel.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <node/interface_ui.h>
#include <node/types.h>
#include <policy/fees/block_policy_estimator.h>
#include <txmempool.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>
#include <util/overflow.h>

#include <array>
#include <chrono>
#include <exception>
#include <fstream>
#include <memory>

#include <QFontMetrics>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QThread>

using common::PSBTError;
using wallet::CCoinControl;

static constexpr std::array confTargets{2, 4, 6, 12, 24, 48, 144, 504, 1008};
int getConfTargetForIndex(int index) {
    if (index+1 > static_cast<int>(confTargets.size())) {
        return confTargets.back();
    }
    if (index < 0) {
        return confTargets[0];
    }
    return confTargets[index];
}
int getIndexForConfTarget(int target) {
    for (unsigned int i = 0; i < confTargets.size(); i++) {
        if (confTargets[i] >= target) {
            return i;
        }
    }
    return confTargets.size() - 1;
}

SendCoinsDialog::SendCoinsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::SendCoinsDialog),
    m_coin_control(new CCoinControl),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);
    ui->labelBalance->setTextFormat(Qt::PlainText);
    ui->labelBalance->setText(tr("Unavailable"));
    ui->labelSmartFee->setTextFormat(Qt::PlainText);

    if (!_platformStyle->getImagesOnButtons()) {
        ui->addButton->setIcon(QIcon());
        ui->clearButton->setIcon(QIcon());
        ui->sendButton->setIcon(QIcon());
    } else {
        ui->addButton->setIcon(_platformStyle->SingleColorIcon(":/icons/add"));
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->sendButton->setIcon(_platformStyle->SingleColorIcon(":/icons/send"));
    }

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);
    ui->lineEditCoinControlChange->setOptional(true);

    addEntry();

    connect(ui->addButton, &QPushButton::clicked, this, &SendCoinsDialog::addEntry);
    connect(ui->clearButton, &QPushButton::clicked, this, &SendCoinsDialog::clear);

    // Coin Control
    connect(ui->pushButtonCoinControl, &QPushButton::clicked, this, &SendCoinsDialog::coinControlButtonClicked);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 7, 0))
    connect(ui->checkBoxCoinControlChange, &QCheckBox::checkStateChanged, this, &SendCoinsDialog::coinControlChangeChecked);
#else
    connect(ui->checkBoxCoinControlChange, &QCheckBox::stateChanged, this, &SendCoinsDialog::coinControlChangeChecked);
#endif
    connect(ui->lineEditCoinControlChange, &QValidatedLineEdit::textEdited, this, &SendCoinsDialog::coinControlChangeEdited);

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardQuantity);
    connect(clipboardAmountAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardAmount);
    connect(clipboardFeeAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardFee);
    connect(clipboardAfterFeeAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardAfterFee);
    connect(clipboardBytesAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardBytes);
    connect(clipboardChangeAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardChange);
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    // init transaction fee section
    QSettings settings;
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    // Preserve nTransactionFee untouched; no scale/policy provenance means it
    // cannot select, populate or approve a native currency/kvB rate.
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    ui->groupFee->setId(ui->radioSmartFee, 0);
    ui->groupFee->setId(ui->radioCustomFee, 1);
    ui->groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->radioCustomFee->setEnabled(false);
    ui->radioCustomFee->setToolTip(tr("Unavailable: explicit native fee-rate policy/context required."));
    connect(ui->customFee, &PgnCurrencyFeeRateField::feeRateStateChanged, this, [this] {
        if (m_custom_fee_binding) return;
        updateFeeSectionControls();
        coinControlUpdateLabels();
        updateFeeMinimizedLabel();
    });
    updateFeeSectionControls();
    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());

    GUIUtil::ExceptionSafeConnect(ui->sendButton, &QPushButton::clicked, this, &SendCoinsDialog::sendButtonClicked);
}

void SendCoinsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, &ClientModel::numBlocksChanged, this, &SendCoinsDialog::updateNumberOfBlocks);
    }
}

void SendCoinsDialog::setModel(WalletModel *_model)
{
    pgn::qt::InvalidateCoinControlLabels(*this);
    ++m_available_request;
    disconnect(m_available_context_changed);
    disconnect(m_display_context_changed);
    disconnect(m_custom_fee_about); disconnect(m_custom_fee_changed);
    m_custom_fee_binding = true;
    ui->customFee->clearRateContext();
    m_custom_fee_binding = false;
    if (model && model->getOptionsModel()) disconnect(model->getOptionsModel(), nullptr, this, nullptr);
    if (model) disconnect(model.data(), nullptr, this, nullptr);
    this->model = _model;
    for (int i = 0; i < ui->entries->count(); ++i) {
        if (auto* entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget())) {
            entry->setModel(_model);
        }
    }
    if (_model && _model->getTransactionTableModel()) {
        connect(_model,&QObject::destroyed,this,[this]{pgn::qt::InvalidateCoinControlLabels(*this);});
        m_available_context_changed = connect(_model->getTransactionTableModel(), &TransactionTableModel::monetaryContextChanged,
            this, &SendCoinsDialog::refreshUseAvailableBalance, Qt::DirectConnection);
        m_display_context_changed = connect(_model->getTransactionTableModel(), &TransactionTableModel::monetaryContextChanged,
            this, &SendCoinsDialog::refreshBalanceDisplay, Qt::DirectConnection);
        m_custom_fee_about = connect(_model->getTransactionTableModel(), &TransactionTableModel::monetaryContextAboutToChange, this, [this] {
            const bool previous = m_custom_fee_binding;
            const QPointer<SendCoinsDialog> alive(this);
            m_custom_fee_binding = true;
            ui->customFee->clearRateContext();
            if (!alive) return;
            pgn::qt::InvalidateCoinControlLabels(*this);
            m_custom_fee_binding = previous;
            if (!previous) updateFeeSectionControls();
        }, Qt::DirectConnection);
        m_custom_fee_changed = connect(_model->getTransactionTableModel(), &TransactionTableModel::monetaryContextChanged,
            this, &SendCoinsDialog::refreshCustomFeeContext, Qt::DirectConnection);
    }
    refreshCustomFeeContext();

    if(_model && _model->getOptionsModel())
    {

        connect(_model, &WalletModel::balanceChanged, this, &SendCoinsDialog::setBalance);
        connect(_model, &WalletModel::overviewBalanceObservationChanged, this, &SendCoinsDialog::refreshBalanceDisplay);
        refreshBalance();

        // Coin Control
        connect(_model->getOptionsModel(), &OptionsModel::coinControlFeaturesChanged, this, &SendCoinsDialog::coinControlFeatureChanged);
        ui->frameCoinControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures());
        coinControlUpdateLabels();

        // fee section
        for (const int n : confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nPowTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, &SendCoinsDialog::updateSmartFeeLabel);
        connect(ui->confTargetSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, &SendCoinsDialog::coinControlUpdateLabels);

        connect(ui->groupFee, &QButtonGroup::idClicked, this, &SendCoinsDialog::updateFeeSectionControls);
        connect(ui->groupFee, &QButtonGroup::idClicked, this, &SendCoinsDialog::coinControlUpdateLabels);

        // Preserve nTransactionFee settings untouched. Do not reinterpret a
        // historical currency/kvB editor as a native transfer or atomic rate.
        updateFeeSectionControls();
        updateSmartFeeLabel();

        if (model->wallet().hasExternalSigner()) {
            //: "device" usually means a hardware wallet.
            ui->sendButton->setText(tr("Sign on device"));
            if (model->getOptionsModel()->hasSigner()) {
                ui->sendButton->setEnabled(true);
                ui->sendButton->setToolTip(tr("Connect your hardware wallet first."));
            } else {
                ui->sendButton->setEnabled(false);
                //: "External signer" means using devices such as hardware wallets.
                ui->sendButton->setToolTip(tr("Set external signer script path in Options -> Wallet"));
            }
        } else if (model->wallet().privateKeysDisabled()) {
            ui->sendButton->setText(tr("Cr&eate Unsigned"));
            ui->sendButton->setToolTip(tr("Creates a Partially Signed Riecoin Transaction (PSRT/PSBT) for use with e.g. an offline %1 wallet, or a PSBT-compatible hardware wallet.").arg(CLIENT_NAME));
        }

        // set the smartfee-sliders default value (wallets default conf.target or last stored value)
        QSettings settings;
        if (settings.value("nSmartFeeSliderPosition").toInt() != 0) {
            // migrate nSmartFeeSliderPosition to nConfTarget
            // nConfTarget is available since 0.15 (replaced nSmartFeeSliderPosition)
            int nConfirmTarget = 25 - settings.value("nSmartFeeSliderPosition").toInt(); // 25 == old slider range
            settings.setValue("nConfTarget", nConfirmTarget);
            settings.remove("nSmartFeeSliderPosition");
        }
        if (settings.value("nConfTarget").toInt() == 0)
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(model->wallet().getConfirmTarget()));
        else
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(settings.value("nConfTarget").toInt()));
    }
    refreshUseAvailableBalance();
}

SendCoinsDialog::~SendCoinsDialog()
{
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    // Unsupported native fee-rate input must not overwrite saved data with0.

    delete ui;
}

bool SendCoinsDialog::PrepareSendText(QString& question_output, QString& informative_output, QString& detailed_output)
{
    question_output.clear(); informative_output.clear(); detailed_output.clear();
    QString question_string, informative_text, detailed_text;
    if (!model || !updateCoinControlState()) return false;
    const QPointer<SendCoinsDialog> alive(this);
    const QPointer<WalletModel> owner(model);
    const auto context = monetaryDisplayContext(); // One copied snapshot per rendering.
    const pgn::qt::SendAmountDisplay display(context);
    const auto unavailable = [&] {
        if (alive) {
            fNewRecipientAllowed = true;
            Q_EMIT message(tr("Send Coins"), tr("Unavailable: exact transaction amounts or denomination context. No confirmation was published."), CClientUIInterface::MSG_WARNING);
        }
        return false;
    };
    if (!display.available()) return unavailable();
    QList<SendCoinsRecipient> recipients;
    bool valid = true;
    CAmount total{};

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            const QPointer<SendCoinsEntry> entry_alive(entry);
            const auto recipient = entry->getValue();
            if (!alive || !owner || !entry_alive) return unavailable();
            const auto sum = recipient ? CheckedAdd(total, recipient->amount) : std::nullopt;
            if(recipient && sum && entry->validate(model->node()))
            {
                total = *sum;
                recipients.append(*recipient);
            }
            else if (valid)
            {
                ui->scrollArea->ensureWidgetVisible(entry);
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty() || !alive || !owner || !displayContextMatches(owner.data(), context))
    {
        return false;
    }

    fNewRecipientAllowed = false;
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if (!alive || !owner || !displayContextMatches(owner.data(), context)) return unavailable();
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        fNewRecipientAllowed = true;
        return false;
    }

    // prepare transaction for getting txFee earlier
    m_current_transaction = std::make_unique<WalletModelTransaction>(recipients);
    WalletModel::SendCoinsReturn prepareStatus;

    if (!updateCoinControlState()) { fNewRecipientAllowed = true; return false; }

    CCoinControl coin_control = *m_coin_control;
    coin_control.m_allow_other_inputs = !coin_control.HasSelected(); // future, could introduce a checkbox to customize this value.
    prepareStatus = model->prepareTransaction(*m_current_transaction, coin_control);

    // process prepareStatus and on error generate message shown to user
    processSendCoinsReturn(prepareStatus); // Never eagerly format a default/unavailable fee.
    if (!alive || !owner) return false;

    if(prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return false;
    }

    if (!alive || !owner || !displayContextMatches(owner.data(), context) || !m_current_transaction->getWtx()) return unavailable();
    CAmount txFee = m_current_transaction->getTransactionFee();
    std::vector<CAmount> recipient_amounts;
    for (const auto& recipient : m_current_transaction->getRecipients()) recipient_amounts.push_back(recipient.amount);
    const auto money = pgn::qt::PresentSendConfirmation(recipient_amounts, txFee, context);
    if (!money) return unavailable();
    QStringList formatted;
    size_t amount_index{0};
    for (const SendCoinsRecipient &rcp : m_current_transaction->getRecipients())
    {
        // generate amount string with wallet name in case of multiwallet
        QString amount = pgn::qt::SendAmountHtml(money.texts->recipients.at(amount_index++), money.texts->scale);
        if (model->isMultiwallet()) {
            const auto wallet_name = pgn::qt::EscapeSendMetadata(model->getWalletName());
            if (!wallet_name) return unavailable();
            amount = tr("%1 from wallet '%2'").arg(amount, *wallet_name);
        }

        // generate address string
        const auto escaped_address = pgn::qt::EscapeSendMetadata(rcp.address);
        const auto escaped_label = pgn::qt::EscapeSendMetadata(rcp.label);
        if (!escaped_address || !escaped_label) return unavailable();
        QString address = *escaped_address;

        QString recipientElement;

        {
            if(rcp.label.length() > 0) // label with address
            {
                recipientElement.append(tr("%1 to '%2'").arg(amount, *escaped_label));
                recipientElement.append(QString(" (%1)").arg(address));
            }
            else // just address
            {
                recipientElement.append(tr("%1 to %2").arg(amount, address));
            }
        }
        formatted.append(recipientElement);
    }

    /*: Message displayed when attempting to create a transaction. Cautionary text to prompt the user to verify
        that the displayed transaction details represent the transaction the user intends to create. */
    question_string.append(tr("Do you want to create this transaction?"));
    question_string.append("<br /><span style='font-size:10pt;'>");
    if (model->wallet().privateKeysDisabled() && !model->wallet().hasExternalSigner()) {
        /*: Text to inform a user attempting to create a transaction of their current options. At this stage,
            a user can only create a PSBT. This string is displayed when private keys are disabled and an external
            signer is not available. */
        question_string.append(tr("Please, review your transaction proposal. This will produce a Partially Signed Riecoin Transaction (PSRT/PSBT) which you can save or copy and then sign with e.g. an offline %1 wallet, or a PSBT-compatible hardware wallet.").arg(CLIENT_NAME));
    } else if (model->getOptionsModel()->getEnablePSBTControls()) {
        /*: Text to inform a user attempting to create a transaction of their current options. At this stage,
            a user can send their transaction or create a PSBT. This string is displayed when both private keys
            and PSBT controls are enabled. */
        question_string.append(tr("Please, review your transaction. You can create and send this transaction or create a Partially Signed Riecoin Transaction (PSRT/PSBT), which you can save or copy and then sign with, e.g., an offline %1 wallet, or a PSBT-compatible hardware wallet.").arg(CLIENT_NAME));
    } else {
        /*: Text to prompt a user to review the details of the transaction they are attempting to send. */
        question_string.append(tr("Please, review your transaction."));
    }
    question_string.append("</span>%1");

    if(txFee >= 0) // Complete tuple includes known fee0; absence already refused.
    {
        // append fee string if a fee is required
        question_string.append("<hr /><b>");
        question_string.append(tr("Transaction fee"));
        question_string.append("</b>");

        // append transaction size
        //: When reviewing a newly created PSBT (via Send flow), the transaction fee is shown, with "virtual size" of the transaction displayed for context
        question_string.append(" (" + tr("%1 kvB", "PSBT transaction creation").arg((double)m_current_transaction->getTransactionSize() / 1000, 0, 'g', 3) + "): ");

        // append transaction fee value
        question_string.append("<span style='color:#aa0000; font-weight:bold;'>");
        question_string.append(pgn::qt::SendAmountHtml(money.texts->fee, money.texts->scale));
        question_string.append("</span><br />");
    }

    // append RBF message
    question_string.append("<span style='font-size:10pt; font-weight:normal;'>");
    question_string.append(tr("You can increase the fee later."));

    // add total amount in all subdivision units
    question_string.append("<hr />");
    question_string.append(QString("<b>%1</b>: <b>%2</b>").arg(tr("Total Amount").toHtmlEscaped(), pgn::qt::SendAmountHtml(money.texts->total, money.texts->scale)));

    if (formatted.size() > 1) {
        question_string = question_string.arg("");
        informative_text = tr("To review recipient list click \"Show Details…\"");
        detailed_text = formatted.join("\n\n");
    } else {
        question_string = question_string.arg("<br /><br />" + formatted.at(0));
    }

    if (!alive || !owner || !displayContextMatches(owner.data(), context)) return unavailable();
    question_output = std::move(question_string);
    informative_output = std::move(informative_text);
    detailed_output = std::move(detailed_text);
    return true;
}

void SendCoinsDialog::presentPSBT(PartiallySignedTransaction& psbtx)
{
    // Serialize the PSBT
    DataStream ssTx{};
    ssTx << psbtx;
    GUIUtil::setClipboard(EncodeBase64(ssTx.str()).c_str());
    QMessageBox msgBox(this);
    //: Caption of "PSBT has been copied" messagebox
    msgBox.setText(tr("Unsigned Transaction", "PSBT copied"));
    msgBox.setInformativeText(tr("The PSBT has been copied to the clipboard. You can also save it."));
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard);
    msgBox.setDefaultButton(QMessageBox::Discard);
    msgBox.setObjectName("psbt_copied_message");
    switch (msgBox.exec()) {
    case QMessageBox::Save: {
        QString selectedFilter;
        const QString fileNameSuggestion = QStringLiteral("transaction.psbt");
        QString filename = GUIUtil::getSaveFileName(this,
            tr("Save Transaction Data"), fileNameSuggestion,
            //: Expanded name of the binary PSBT file format. See: BIP 174.
            tr("Partially Signed Transaction (Binary)") + QLatin1String(" (*.psbt)"), &selectedFilter);
        if (filename.isEmpty()) {
            return;
        }
        std::ofstream out{filename.toLocal8Bit().data(), std::ofstream::out | std::ofstream::binary};
        out << ssTx.str();
        out.close();
        //: Popup message when a PSBT has been saved to a file
        Q_EMIT message(tr("PSBT saved"), tr("PSBT saved to disk"), CClientUIInterface::MSG_INFORMATION);
        break;
    }
    case QMessageBox::Discard:
        break;
    default:
        assert(false);
    } // msgBox.exec()
}

bool SendCoinsDialog::signWithExternalSigner(PartiallySignedTransaction& psbtx, CMutableTransaction& mtx, bool& complete) {
    std::optional<PSBTError> err;
    try {
        err = model->wallet().fillPSBT({.sign = true, .bip32_derivs = true}, /*n_signed=*/nullptr, psbtx, complete);
    } catch (const std::runtime_error& e) {
        QMessageBox::critical(nullptr, tr("Sign failed"), e.what());
        return false;
    }
    if (err == PSBTError::EXTERNAL_SIGNER_NOT_FOUND) {
        //: "External signer" means using devices such as hardware wallets.
        const QString msg = tr("External signer not found");
        QMessageBox::critical(nullptr, msg, msg);
        return false;
    }
    if (err == PSBTError::EXTERNAL_SIGNER_FAILED) {
        //: "External signer" means using devices such as hardware wallets.
        const QString msg = tr("External signer failure");
        QMessageBox::critical(nullptr, msg, msg);
        return false;
    }
    if (err) {
        qWarning() << "Failed to sign PSBT";
        processSendCoinsReturn(WalletModel::TransactionCreationFailed);
        return false;
    }
    // fillPSBT does not always properly finalize
    complete = FinalizeAndExtractPSBT(psbtx, mtx);
    return true;
}

void SendCoinsDialog::sendButtonClicked([[maybe_unused]] bool checked)
{
    if(!model || !model->getOptionsModel())
        return;

    QString question_string, informative_text, detailed_text;
    if (!PrepareSendText(question_string, informative_text, detailed_text)) return;
    assert(m_current_transaction);

    const QString confirmation = tr("Confirm send coins");
    const bool enable_send{!model->wallet().privateKeysDisabled() || model->wallet().hasExternalSigner()};
    const bool always_show_unsigned{model->getOptionsModel()->getEnablePSBTControls()};
    auto confirmationDialog = new SendConfirmationDialog(confirmation, question_string, informative_text, detailed_text, SEND_CONFIRM_DELAY, enable_send, always_show_unsigned, this);
    confirmationDialog->setAttribute(Qt::WA_DeleteOnClose);
    // TODO: Replace QDialog::exec() with safer QDialog::show().
    const auto retval = static_cast<QMessageBox::StandardButton>(confirmationDialog->exec());

    if(retval != QMessageBox::Yes && retval != QMessageBox::Save)
    {
        fNewRecipientAllowed = true;
        return;
    }

    bool send_failure = false;
    if (retval == QMessageBox::Save) {
        // "Create Unsigned" clicked
        CMutableTransaction mtx = CMutableTransaction{*(m_current_transaction->getWtx())};
        PartiallySignedTransaction psbtx(mtx);
        bool complete = false;
        // Fill without signing
        const auto err{model->wallet().fillPSBT({.sign = false, .bip32_derivs = true}, /*n_signed=*/nullptr, psbtx, complete)};
        assert(!complete);
        assert(!err);

        // Copy PSBT to clipboard and offer to save
        presentPSBT(psbtx);
    } else {
        // "Send" clicked
        assert(!model->wallet().privateKeysDisabled() || model->wallet().hasExternalSigner());
        bool broadcast = true;
        if (model->wallet().hasExternalSigner()) {
            CMutableTransaction mtx = CMutableTransaction{*(m_current_transaction->getWtx())};
            PartiallySignedTransaction psbtx(mtx);
            bool complete = false;
            // Always fill without signing first. This prevents an external signer
            // from being called prematurely and is not expensive.
            const auto err{model->wallet().fillPSBT({.sign = false, .bip32_derivs = true}, /*n_signed=*/nullptr, psbtx, complete)};
            assert(!complete);
            assert(!err);
            send_failure = !signWithExternalSigner(psbtx, mtx, complete);
            // Don't broadcast when user rejects it on the device or there's a failure:
            broadcast = complete && !send_failure;
            if (!send_failure) {
                // A transaction signed with an external signer is not always complete,
                // e.g. in a multisig wallet.
                if (complete) {
                    // Prepare transaction for broadcast transaction if complete
                    const CTransactionRef tx = MakeTransactionRef(mtx);
                    m_current_transaction->setWtx(tx);
                } else {
                    presentPSBT(psbtx);
                }
            }
        }

        // Broadcast the transaction, unless an external signer was used and it
        // failed, or more signatures are needed.
        if (broadcast) {
            // now send the prepared transaction
            model->sendCoins(*m_current_transaction);
            Q_EMIT coinsSent(m_current_transaction->getWtx()->GetHash());
        }
    }
    if (!send_failure) {
        accept();
        m_coin_control->UnSelectAll();
        coinControlUpdateLabels();
    }
    fNewRecipientAllowed = true;
    m_current_transaction.reset();
}

void SendCoinsDialog::clear()
{
    m_current_transaction.reset();

    // Clear coin control settings
    m_coin_control->UnSelectAll();
    ui->checkBoxCoinControlChange->setChecked(false);
    ui->lineEditCoinControlChange->clear();
    coinControlUpdateLabels();

    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();
}

void SendCoinsDialog::reject()
{
    clear();
}

void SendCoinsDialog::accept()
{
    clear();
}

SendCoinsEntry *SendCoinsDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, &SendCoinsEntry::removeEntry, this, &SendCoinsDialog::removeEntry);
    connect(entry, &SendCoinsEntry::useAvailableBalance, this, &SendCoinsDialog::useAvailableBalance);
    connect(entry, &SendCoinsEntry::payAmountChanged, this, &SendCoinsDialog::coinControlUpdateLabels);
    connect(entry, &SendCoinsEntry::subtractFeeFromAmountChanged, this, &SendCoinsDialog::coinControlUpdateLabels);

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());

    // Scroll to the newly added entry on a QueuedConnection because Qt doesn't
    // adjust the scroll area and scrollbar immediately when the widget is added.
    // Invoking on a DirectConnection will only scroll to the second-to-last entry.
    QMetaObject::invokeMethod(ui->scrollArea, [this] {
        if (ui->scrollArea->verticalScrollBar()) {
            ui->scrollArea->verticalScrollBar()->setValue(ui->scrollArea->verticalScrollBar()->maximum());
        }
    }, Qt::QueuedConnection);

    updateTabsAndLabels();
    refreshUseAvailableBalance();
    return entry;
}

void SendCoinsDialog::updateTabsAndLabels()
{
    setupTabChain(nullptr);
    coinControlUpdateLabels();
}

void SendCoinsDialog::removeEntry(SendCoinsEntry* entry)
{
    ++m_available_request;
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *SendCoinsDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    return ui->addButton;
}

void SendCoinsDialog::setAddress(const QString &address)
{
    SendCoinsEntry *entry = nullptr;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void SendCoinsDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = nullptr;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
    updateTabsAndLabels();
}

bool SendCoinsDialog::handlePaymentRequest(const SendCoinsRecipient &rv)
{
    (void)rv;
    // Legacy DTO has no exact scale/context provenance (old URI parser uses8).
    // Do not promote that decoded integer into an explicit native policy.
    setToolTip(tr("Unavailable: legacy payment input lacks native amount/context provenance."));
    return false;
}

void SendCoinsDialog::setBalance(const interfaces::WalletBalances& balances)
{
    Q_UNUSED(balances);
    refreshBalanceDisplay(); // A signal/cache DTO is NOT a new observation.
}

std::optional<pgn::core::MonetaryContext> SendCoinsDialog::monetaryDisplayContext() const
{
    if (QThread::currentThread() != thread() || !model || model->thread() != thread()) return std::nullopt;
    const auto* table = model->getTransactionTableModel();
    return table && table->thread() == thread() ? table->monetaryContext() : std::nullopt;
}

bool SendCoinsDialog::displayContextMatches(WalletModel* owner, const std::optional<pgn::core::MonetaryContext>& context) const
{
    return owner && model.data() == owner && context &&
        pgn::qt::SameTransactionMonetaryContext(context, monetaryDisplayContext());
}

void SendCoinsDialog::refreshBalanceDisplay()
{
    if (QThread::currentThread() != thread()) return;
    const QPointer<WalletModel> owner(model);
    const auto context = monetaryDisplayContext();
    const auto result = pgn::qt::PresentOverviewBalances(owner ? owner->getOverviewBalanceObservation() : std::nullopt, context, false);
    ui->labelBalance->setText(tr("Unavailable"));
    ui->labelBalance->setToolTip(QString{});
    if (!result || !displayContextMatches(owner.data(), context)) return;
    bool external{false};
    try { external = owner->wallet().hasExternalSigner(); }
    catch (const std::exception&) { return; }
    const QString value = QStringLiteral("%1 (%2)").arg(result.amounts->at(0), *result.scale);
    ui->labelBalance->setText(result.status == pgn::qt::OverviewBalanceStatus::LastObserved ? tr("Last observed: %1").arg(value) : value);
    ui->labelBalance->setToolTip(tr("Observed at %1 UTC; not a spendable balance or network freshness proof.").arg(result.observed_at->toString(Qt::ISODateWithMs)));
    ui->labelBalanceName->setText(external ? tr("External balance:") : tr("Balance:"));
}

void SendCoinsDialog::refreshBalance()
{
    refreshBalanceDisplay();
    updateSmartFeeLabel();
}

void SendCoinsDialog::processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to SendCoinsDialog usage of WalletModel::SendCoinsReturn.
    // All status values are used only in WalletModel::prepareTransaction()
    switch(sendCoinsReturn.status)
    {
    case WalletModel::InvalidAddress:
        msgParams.first = tr("The recipient address is not valid. Please recheck.");
        break;
    case WalletModel::InvalidAmount:
        msgParams.first = tr("Amounts must be positive and fit the configured exact transfer range and total.");
        break;
    case WalletModel::AmountExceedsBalance:
        msgParams.first = tr("The amount exceeds your balance.");
        break;
    case WalletModel::MonetaryUnavailable:
        msgParams.first = tr("Unavailable: native coin-control candidates or monetary context. No transaction was prepared.");
        break;
    case WalletModel::DuplicateAddress:
        msgParams.first = tr("Duplicate address found: addresses should only be used once each.");
        break;
    case WalletModel::TransactionCreationFailed:
        msgParams.first = tr("Transaction creation failed!");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::AbsurdFee:
    {
        const auto context = monetaryDisplayContext();
        const pgn::qt::SendAmountDisplay display(context);
        const QPointer<WalletModel> owner(model);
        std::optional<QString> amount;
        try { if (owner && display.available()) amount = display.number(owner->wallet().getDefaultMaxTxFee(), pgn::qt::SendAmountKind::Aggregate); }
        catch (const std::exception&) { amount.reset(); }
        if (!displayContextMatches(owner.data(), context)) amount.reset();
        const auto scale = display.scale();
        msgParams.first = amount && scale ? tr("A fee higher than %1 (%2) exceeds the configured fee limit.").arg(*amount, *scale)
            : tr("Unavailable: exact configured fee limit or denomination context.");
        break;
    }
    case WalletModel::OK:
        return;
    } // no default case, so the compiler can warn about missing cases
    Q_EMIT message(tr("Send Coins"), msgParams.first, msgParams.second);
}

void SendCoinsDialog::minimizeFeeSection(bool fMinimize)
{
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee  ->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void SendCoinsDialog::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void SendCoinsDialog::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void SendCoinsDialog::useAvailableBalance(SendCoinsEntry* entry)
{
    const QPointer<SendCoinsDialog> alive(this);
    const QPointer<WalletModel> owner(model);
    const QPointer<SendCoinsEntry> target(entry);
    if (!owner || !target || QThread::currentThread() != thread()) return;
    const quint64 request = ++m_available_request;
    const auto expected = target->amountContext();
    const QString previous = target->amountInputText();
    std::vector<std::optional<CAmount>> amounts;
    std::vector<std::pair<QPointer<SendCoinsEntry>, QString>> inputs;
    bool target_present{false};
    for (int i = 0; i < ui->entries->count(); ++i) {
        auto* other = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if (other == target && !other->isHidden()) target_present = true;
        if (!other || other->isHidden() || other == target) continue;
        const QPointer<SendCoinsEntry> other_alive(other);
        const auto value = other->getValue();
        if (!alive || !owner || !target || !other_alive) return;
        amounts.push_back(value ? std::optional<CAmount>{value->amount} : std::nullopt);
        inputs.emplace_back(other_alive, other->amountInputText());
        if (!pgn::qt::SameTransactionMonetaryContext(expected, other->amountContext())) amounts.back().reset();
    }
    if (!target_present || !alive || !owner || !target || model != owner || request != m_available_request) return;
    if (!pgn::qt::SumTransferAmounts(expected, amounts)) {
        target->setToolTip(tr("Unavailable: another exact amount or denomination is missing or invalid. Existing input was preserved."));
        return;
    }
    CCoinControl query{*m_coin_control};
    query.m_allow_other_inputs = !query.HasSelected(); // Upstream selected-only semantics.
    const auto candidates = owner->getCoinControlBalance(query);
    if (!alive || !owner || !target || model != owner || m_available_request != request) return;
    if (target->isHidden() || target->amountInputText() != previous || !pgn::qt::SameTransactionMonetaryContext(expected, target->amountContext())) return;
    for (const auto& [other, text] : inputs) {
        if (!other || other->isHidden() || other->amountInputText() != text || !pgn::qt::SameTransactionMonetaryContext(expected, other->amountContext())) return;
    }
    const auto staged = pgn::qt::StageCoinControlRemainder(candidates, expected, amounts);
    if (!staged || !expected || !target->applyAvailableAmount(*owner, *expected, *staged.amount)) {
        if (target) {
            if (!candidates) target->setUseAvailableContext(std::nullopt);
            target->setToolTip(tr("Unavailable: candidate query, other amounts, context, or transfer range. Existing input was preserved; no amount was clamped."));
        }
    }
}

void SendCoinsDialog::refreshUseAvailableBalance()
{
    if (QThread::currentThread() != thread() || m_available_refreshing) return;
    for (int i = 0; i < ui->entries->count(); ++i) {
        if (auto* entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget())) entry->setUseAvailableContext(std::nullopt);
    }
    if (!model) return;
    m_available_refreshing = true;
    const QPointer<SendCoinsDialog> alive(this);
    const QPointer<WalletModel> owner(model);
    const quint64 request = ++m_available_request;
    CCoinControl query{*m_coin_control};
    query.m_allow_other_inputs = !query.HasSelected();
    const auto result = owner->getCoinControlBalance(query);
    if (!alive) return;
    m_available_refreshing = false;
    if (!owner || model != owner || request != m_available_request || !result) return;
    // No amount/hash stored; every click MUST issue a fresh query.
    for (int i = 0; i < ui->entries->count(); ++i) {
        if (auto* entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget())) entry->setUseAvailableContext(result.snapshot->context);
    }
}

void SendCoinsDialog::updateFeeSectionControls()
{
    const bool configured = ui->customFee->configured();
    ui->radioCustomFee->setEnabled(configured);
    ui->labelCustomPerKilobyte->setText(ui->customFee->unitText());
    ui->confTargetSelector      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee           ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee2          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee3          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelFeeEstimation      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelCustomPerKilobyte  ->setEnabled(ui->radioCustomFee->isChecked());
    ui->customFee               ->setEnabled(configured && ui->radioCustomFee->isChecked());
}

void SendCoinsDialog::refreshCustomFeeContext()
{
    if (QThread::currentThread() != thread() || m_custom_fee_binding) return;
    const QPointer<SendCoinsDialog> alive(this);
    const QPointer<WalletModel> owner(model);
    const auto context = monetaryDisplayContext();
    std::optional<pgn::core::AtomicFeeRatePolicy> rate;
    try { if (owner) rate = SelectedPgnAtomicFeeRatePolicy(); }
    catch (const std::exception&) { rate.reset(); }
    m_custom_fee_binding = true;
    if (owner && displayContextMatches(owner.data(), context)) ui->customFee->bindRateContext(*owner, context, rate);
    else ui->customFee->clearRateContext();
    if (!alive) return;
    m_custom_fee_binding = false;
    updateFeeSectionControls();
    updateFeeMinimizedLabel();
}

void SendCoinsDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        const QPointer<WalletModel> owner(model);
        const auto context = monetaryDisplayContext();
        std::optional<pgn::core::AtomicFeeRatePolicy> policy;
        try { policy = SelectedPgnAtomicFeeRatePolicy(); } catch (const std::exception&) {}
        const auto result = owner ? ui->customFee->feeRateResult(*owner,context,policy) : pgn::qt::CurrencyFeeRateResult{};
        const pgn::qt::SendAmountDisplay display(context);
        const auto text = result ? display.number(*result.atomic_per_kvbyte,pgn::qt::SendAmountKind::CurrencyPerKVByte) : std::nullopt;
        const auto scale = display.scale();
        ui->labelFeeMinimized->setText(text && scale && displayContextMatches(owner.data(),context)
            ? tr("%1 units/kvB (%2)").arg(*text,*scale) : tr("Unavailable: native fee-rate input/policy"));
    }
}

bool SendCoinsDialog::updateCoinControlState()
{
    if (m_custom_fee_binding) return false;
    const QPointer<WalletModel> owner(model);
    const auto context = monetaryDisplayContext();
    std::optional<pgn::core::AtomicFeeRatePolicy> policy;
    // No rate policy default and no rate/transfer ceiling conflation.
    try {
        policy = SelectedPgnAtomicFeeRatePolicy();
        if (!owner || !context || !policy || !displayContextMatches(owner.data(),context)) return false;
    } catch (const std::exception&) {
        return false; // Local configuration/read error, never a rate or zero.
    }
    if (ui->radioCustomFee->isChecked()) {
        const auto result = ui->customFee->feeRateResult(*owner,context,policy);
        if (!result || !displayContextMatches(owner.data(),context)) return false;
        m_coin_control->m_feerate = *result.rate; // Native conversion mandatory.
    } else {
        m_coin_control->m_feerate.reset(); // User explicitly chose automatic.
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    m_coin_control->m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
    return true;
}

void SendCoinsDialog::updateNumberOfBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool headers, SynchronizationState sync_state) {
    // During shutdown, clientModel will be nullptr. Attempting to update views at this point may cause a crash
    // due to accessing backend models that might no longer exist.
    if (!clientModel) return;
    // Process event
    if (sync_state == SynchronizationState::POST_INIT) {
        updateSmartFeeLabel();
    }
}

void SendCoinsDialog::updateSmartFeeLabel()
{
    const QPointer<SendCoinsDialog> alive(this);
    const auto unavailable = [this, alive] {
        if (!alive) return;
        ui->labelSmartFee->setText(tr("Unavailable: native fee-rate context"));
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->clear();
        ui->fallbackFeeWarningLabel->hide();
        updateFeeMinimizedLabel();
    };
    const QPointer<WalletModel> owner(model);
    const auto context = monetaryDisplayContext();
    const pgn::qt::SendAmountDisplay display(context);
    if (!owner || !owner->getOptionsModel() || !display.available() || !updateCoinControlState()) { unavailable(); return; }
    CCoinControl automatic{*m_coin_control};
    automatic.m_feerate.reset(); // Informational query must not erase custom choice.
    int returned_target{-1};
    FeeReason reason{};
    std::optional<QString> fee_text;
    try {
        // This scalar is the atomic fee for1000vB, i.e.currency/kvB after
        // exact denomination formatting. No naked CFeeRate query/conversion.
        fee_text = display.number(owner->wallet().getMinimumFee(1000, automatic, &returned_target, &reason), pgn::qt::SendAmountKind::CurrencyPerKVByte);
    } catch (const std::exception&) { unavailable(); return; }
    if (!alive) return;
    const auto scale = display.scale();
    if (!fee_text || !scale || !displayContextMatches(owner.data(), context)) { unavailable(); return; }
    ui->labelSmartFee->setText(tr("%1 units/kvB (%2)").arg(*fee_text, *scale));

    if (reason == FeeReason::FALLBACK) {
        ui->labelSmartFee2->show(); // (Smart fee not initialized yet. This usually takes a few blocks...)
        ui->labelFeeEstimation->setText("");
        ui->fallbackFeeWarningLabel->setVisible(true);
        int lightness = ui->fallbackFeeWarningLabel->palette().color(QPalette::WindowText).lightness();
        QColor warning_colour(255 - (lightness / 5), 176 - (lightness / 3), 48 - (lightness / 14));
        ui->fallbackFeeWarningLabel->setStyleSheet("QLabel { color: " + warning_colour.name() + "; }");
        ui->fallbackFeeWarningLabel->setIndent(GUIUtil::TextWidth(QFontMetrics(ui->fallbackFeeWarningLabel->font()), "x"));
    }
    else
    {
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->setText(returned_target > 0 ? tr("Estimated to begin confirmation within %n block(s).", "", returned_target) : tr("Confirmation estimate unavailable"));
        ui->fallbackFeeWarningLabel->setVisible(false);
    }

    updateFeeMinimizedLabel();
}

// Coin Control: copy label "Quantity" to clipboard
void SendCoinsDialog::coinControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void SendCoinsDialog::coinControlClipboardAmount()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlAmount,monetaryDisplayContext(),GUIUtil::setClipboard);
}

// Coin Control: copy label "Fee" to clipboard
void SendCoinsDialog::coinControlClipboardFee()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlFee,monetaryDisplayContext(),GUIUtil::setClipboard);
}

// Coin Control: copy label "After fee" to clipboard
void SendCoinsDialog::coinControlClipboardAfterFee()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlAfterFee,monetaryDisplayContext(),GUIUtil::setClipboard);
}

// Coin Control: copy label "Bytes" to clipboard
void SendCoinsDialog::coinControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Change" to clipboard
void SendCoinsDialog::coinControlClipboardChange()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlChange,monetaryDisplayContext(),GUIUtil::setClipboard);
}

// Coin Control: settings menu - coin control enabled/disabled by user
void SendCoinsDialog::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);

    if (!checked && model) { // coin control features disabled
        m_coin_control = std::make_unique<CCoinControl>();
    }

    coinControlUpdateLabels();
    refreshUseAvailableBalance();
}

// Coin Control: button inputs -> show actual coin control dialog
void SendCoinsDialog::coinControlButtonClicked()
{
    auto dlg = new CoinControlDialog(*m_coin_control, model, platformStyle);
    connect(dlg, &QDialog::finished, this, &SendCoinsDialog::coinControlUpdateLabels);
    connect(dlg, &QDialog::finished, this, &SendCoinsDialog::refreshUseAvailableBalance);
    GUIUtil::ShowModalDialogAsynchronously(dlg);
}

// Coin Control: checkbox custom change address
#if (QT_VERSION >= QT_VERSION_CHECK(6, 7, 0))
void SendCoinsDialog::coinControlChangeChecked(Qt::CheckState state)
#else
void SendCoinsDialog::coinControlChangeChecked(int state)
#endif
{
    if (state == Qt::Unchecked)
    {
        m_coin_control->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        coinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void SendCoinsDialog::coinControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        m_coin_control->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid Riecoin address"));
        }
        else // Valid address
        {
            if (!model->wallet().isSpendable(dest)) {
                ui->labelCoinControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    m_coin_control->destChange = dest;
                else
                {
                    ui->lineEditCoinControlChange->setText("");
                    ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");
                    ui->labelCoinControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");

                // Query label
                QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelCoinControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelCoinControlChangeLabel->setText(tr("(no label)"));

                m_coin_control->destChange = dest;
            }
        }
    }
}

// Coin Control: update labels
void SendCoinsDialog::coinControlUpdateLabels()
{
    // Every refused preparation must revoke the previous quote, even when the
    // containing panel is hidden. Hidden text is not a valid clipboard amount.
    pgn::qt::InvalidateCoinControlLabels(*this);
    if (!model || !model->getOptionsModel())
        return;

    if (!updateCoinControlState()) {
        ui->labelCoinControlAutomaticallySelected->setText(tr("Unavailable: native amount/rate context"));
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        return;
    }

    // set pay amounts
    QList<CAmount> pay_amounts;
    bool subtract_fee = false;
    CAmount pay_total{};

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            const auto rcp = entry->getValue();
            const auto sum = rcp ? CheckedAdd(pay_total, rcp->amount) : std::nullopt;
            if (!rcp || !sum) {
                ui->labelCoinControlAutomaticallySelected->setText(tr("Unavailable: exact transfer amount"));
                ui->labelCoinControlAutomaticallySelected->show();
                ui->widgetCoinControl->hide();
                return; // No partial publication or invalid->zero row.
            }
            pay_total = *sum;
            pay_amounts.append(rcp->amount);
            if (rcp->fSubtractFeeFromAmount) subtract_fee = true;
        }
    }

    CoinControlDialog::payAmounts = pay_amounts;
    CoinControlDialog::fSubtractFeeFromAmount = subtract_fee;
    if (m_coin_control->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels(*m_coin_control, model, this);

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

SendConfirmationDialog::SendConfirmationDialog(const QString& title, const QString& text, const QString& informative_text, const QString& detailed_text, int _secDelay, bool enable_send, bool always_show_unsigned, QWidget* parent)
    : QMessageBox(parent), secDelay(_secDelay), m_enable_send(enable_send)
{
    setIcon(QMessageBox::Question);
    setWindowTitle(title); // On macOS, the window title is ignored (as required by the macOS Guidelines).
    setText(text);
    setInformativeText(informative_text);
    setDetailedText(detailed_text);
    setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (always_show_unsigned || !enable_send) addButton(QMessageBox::Save);
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    if (confirmButtonText.isEmpty()) {
        confirmButtonText = yesButton->text();
    }
    m_psbt_button = button(QMessageBox::Save);
    updateButtons();
    connect(&countDownTimer, &QTimer::timeout, this, &SendConfirmationDialog::countDown);
}

int SendConfirmationDialog::exec()
{
    updateButtons();
    countDownTimer.start(1s);
    return QMessageBox::exec();
}

void SendConfirmationDialog::countDown()
{
    secDelay--;
    updateButtons();

    if(secDelay <= 0)
    {
        countDownTimer.stop();
    }
}

void SendConfirmationDialog::updateButtons()
{
    if(secDelay > 0)
    {
        yesButton->setEnabled(false);
        yesButton->setText(confirmButtonText + (m_enable_send ? (" (" + QString::number(secDelay) + ")") : QString("")));
        if (m_psbt_button) {
            m_psbt_button->setEnabled(false);
            m_psbt_button->setText(m_psbt_button_text + " (" + QString::number(secDelay) + ")");
        }
    }
    else
    {
        yesButton->setEnabled(m_enable_send);
        yesButton->setText(confirmButtonText);
        if (m_psbt_button) {
            m_psbt_button->setEnabled(true);
            m_psbt_button->setText(m_psbt_button_text);
        }
    }
}
