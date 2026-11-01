// Copyright (c) 2011-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/walletmodel.h>

#include <qt/addresstablemodel.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/paymentserver.h>
#include <qt/pgntransactionamount.h>
#include <qt/pgnconsumeramount.h>
#include <qt/pgnsendamountdisplay.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/sendcoinsdialog.h>
#include <qt/transactiontablemodel.h>

#include <common/args.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <node/interface_ui.h>
#include <node/types.h>
#include <psbt.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>
#include <wallet/wallet.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <vector>

#include <QDebug>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QThread>

using wallet::CCoinControl;
using wallet::CRecipient;
using wallet::DEFAULT_DISABLE_WALLET;

WalletModel::WalletModel(std::unique_ptr<interfaces::Wallet> wallet, ClientModel& client_model, const PlatformStyle *platformStyle, QObject *parent) :
    QObject(parent),
    m_wallet(std::move(wallet)),
    m_client_model(&client_model),
    m_node(client_model.node()),
    optionsModel(client_model.getOptionsModel()),
    timer(new QTimer(this))
{
    addressTableModel = new AddressTableModel(this);
    transactionTableModel = new TransactionTableModel(platformStyle, this);
    recentRequestsTableModel = new RecentRequestsTableModel(this);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

void WalletModel::startPollBalance()
{
    // Update the cached balance right away, so every view can make use of it,
    // so them don't need to waste resources recalculating it.
    pollBalanceChanged();

    // This timer will be fired repeatedly to update the balance
    // Since the QTimer::timeout is a private signal, it cannot be used
    // in the GUIUtil::ExceptionSafeConnect directly.
    connect(timer, &QTimer::timeout, this, &WalletModel::timerTimeout);
    GUIUtil::ExceptionSafeConnect(this, &WalletModel::timerTimeout, this, &WalletModel::pollBalanceChanged);
    timer->start(MODEL_UPDATE_DELAY);
}

void WalletModel::setClientModel(ClientModel* client_model)
{
    m_client_model = client_model;
    if (!m_client_model) timer->stop();
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus) {
        Q_EMIT encryptionStatusChanged();
    }
}

void WalletModel::pollBalanceChanged()
{
    if (thread() != QThread::currentThread() || !transactionTableModel ||
        !transactionTableModel->refreshMonetaryContext()) return;
    const auto context = transactionTableModel->monetaryContext();
    const auto last_observation = m_overview_balances.snapshot();
    // Avoid recomputing wallet balances unless a TransactionChanged or
    // BlockTip notification was received, metadata changed or a read failed.
    if (!fForceCheckBalanceChanged && m_cached_last_update_tip == getLastBlockProcessed() &&
        last_observation && last_observation->state == pgn::qt::OverviewObservationState::Observed &&
        pgn::qt::SameTransactionMonetaryContext(last_observation->context, context)) return;

    // Try to get balances and return early if locks can't be acquired. This
    // avoids the GUI from getting stuck on periodical polls if the core is
    // holding the locks for a longer time - for example, during a wallet
    // rescan.
    interfaces::WalletBalances new_balances;
    uint256 block_hash;
    if (!m_wallet->tryGetBalances(new_balances, block_hash)) {
        if (m_overview_balances.requireRefresh()) Q_EMIT overviewBalanceObservationChanged();
        return;
    }

    // The ONLY production admission to the optional Overview observation cache.
    // WalletModel existence/default DTO/direct getCachedBalance never enters it.
    const auto observation = m_overview_balances.acceptSuccess(new_balances, context, QDateTime::currentDateTimeUtc());
    if (observation == pgn::qt::OverviewObservationUpdate::InvalidTime) {
        if (m_overview_balances.requireRefresh()) Q_EMIT overviewBalanceObservationChanged();
        return;
    }
    const bool first_observation = observation == pgn::qt::OverviewObservationUpdate::First;

    if (fForceCheckBalanceChanged || block_hash != m_cached_last_update_tip || first_observation) {
        fForceCheckBalanceChanged = false;

        // Balance and number of transactions might have changed
        m_cached_last_update_tip = block_hash;

        checkBalanceChanged(new_balances, first_observation);
        if(transactionTableModel)
            transactionTableModel->updateConfirmations();
    }
    Q_EMIT overviewBalanceObservationChanged();
}

void WalletModel::checkBalanceChanged(const interfaces::WalletBalances& new_balances, bool first_observation)
{
    if (first_observation || new_balances.balanceChanged(m_cached_balances)) {
        m_cached_balances = new_balances;
        Q_EMIT balanceChanged(new_balances);
    }
}

interfaces::WalletBalances WalletModel::getCachedBalance() const
{
    return m_cached_balances;
}

std::optional<pgn::qt::OverviewBalanceObservation> WalletModel::getOverviewBalanceObservation() const
{
    if (thread() != QThread::currentThread()) return std::nullopt;
    return m_overview_balances.snapshot();
}

void WalletModel::updateTransaction()
{
    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateAddressBook(const QString &address, const QString &label,
        bool isMine, wallet::AddressPurpose purpose, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, purpose, status);
}

bool WalletModel::validateAddress(const QString& address) const
{
    return IsValidDestinationString(address.toStdString());
}

WalletModel::SendCoinsReturn WalletModel::prepareTransaction(WalletModelTransaction &transaction, const CCoinControl& coinControl)
{
    transaction.getWtx() = nullptr; // reset tx output

    const QPointer<WalletModel> alive(this);
    const QPointer<TransactionTableModel> table(transactionTableModel);
    if (QThread::currentThread() != thread() || !table || table->thread() != thread()) return MonetaryUnavailable;
    std::optional<pgn::core::MonetaryContext> context;
    try {
        if (!table->refreshMonetaryContext() || !alive || !table) return MonetaryUnavailable;
        context = table->monetaryContext();
    } catch (const std::exception&) { return MonetaryUnavailable; }
    if (!context || pgn::core::ValidateMonetaryContext(*context) != pgn::core::ExactAmountError::None) return MonetaryUnavailable;

    CAmount total = 0;
    bool fSubtractFeeFromAmount = false;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<CRecipient> vecSend;
    std::vector<std::optional<CAmount>> recipient_amounts;

    if(recipients.empty())
    {
        return OK;
    }

    QSet<QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;

    // Pre-check input data for validity
    for (const SendCoinsRecipient &rcp : recipients)
    {
        if (rcp.fSubtractFeeFromAmount)
            fSubtractFeeFromAmount = true;
        {   // User-entered Riecoin address / amount:
            if(!validateAddress(rcp.address))
            {
                return InvalidAddress;
            }
            if(rcp.amount <= 0 || rcp.amount > context->transfer.max_atomic)
            {
                return InvalidAmount;
            }
            setAddress.insert(rcp.address);
            ++nAddresses;

            vecSend.emplace_back(CRecipient{DecodeDestination(rcp.address.toStdString()), rcp.amount, rcp.fSubtractFeeFromAmount});

            recipient_amounts.push_back(rcp.amount);
        }
    }
    const auto summed = pgn::qt::SumTransferAmounts(context, recipient_amounts);
    if (!summed) return InvalidAmount;
    total = *summed.amount;
    if(setAddress.size() != nAddresses)
    {
        return DuplicateAddress;
    }

    // A missing provider/context is not a zero balance or insufficient funds.
    const auto candidates = getCoinControlBalance(coinControl);
    if (!alive || !candidates || !pgn::qt::SameTransactionMonetaryContext(context, candidates.snapshot->context)) return MonetaryUnavailable;
    const CAmount nBalance = candidates.snapshot->amount;

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    try {
        auto& newTx = transaction.getWtx();
        const auto& res = m_wallet->createTransaction(vecSend, coinControl, /*sign=*/!wallet().privateKeysDisabled(), /*change_pos=*/std::nullopt);
        if (!res) {
            Q_EMIT message(tr("Send Coins"), QString::fromStdString(util::ErrorString(res).translated),
                           CClientUIInterface::MSG_ERROR);
            return TransactionCreationFailed;
        }

        newTx = res->tx;
        CAmount nFeeRequired = res->fee;
        transaction.setTransactionFee(nFeeRequired);
        if (fSubtractFeeFromAmount && newTx) {
            transaction.reassignAmounts(static_cast<int>(res->change_pos.value_or(-1)));
        }

        // Reject absurdly high fee. (This can never happen because the
        // wallet never creates transactions with fee greater than
        // m_default_max_tx_fee. This merely a belt-and-suspenders check).
        if (nFeeRequired > m_wallet->getDefaultMaxTxFee()) {
            return AbsurdFee;
        }
    } catch (const std::runtime_error& err) {
        // Something unexpected happened, instruct user to report this bug.
        Q_EMIT message(tr("Send Coins"), QString::fromStdString(err.what()),
                       CClientUIInterface::MSG_ERROR);
        return TransactionCreationFailed;
    }

    return SendCoinsReturn(OK);
}

void WalletModel::sendCoins(WalletModelTransaction& transaction)
{
    QByteArray transaction_array; /* store serialized transaction */

    {
        std::vector<std::pair<std::string, std::string>> vOrderForm;
        for (const SendCoinsRecipient &rcp : transaction.getRecipients())
        {
            if (!rcp.message.isEmpty()) // Message from normal bitcoin:URI (bitcoin:123...?message=example)
                vOrderForm.emplace_back("Message", rcp.message.toStdString());
        }

        auto& newTx = transaction.getWtx();
        wallet().commitTransaction(newTx, /*value_map=*/{}, std::move(vOrderForm));

        DataStream ssTx;
        ssTx << TX_WITH_WITNESS(*newTx);
        transaction_array.append((const char*)ssTx.data(), ssTx.size());
    }

    // Add addresses / update labels that we've sent to the address book,
    // and emit coinsSent signal for each recipient
    for (const SendCoinsRecipient &rcp : transaction.getRecipients())
    {
        {
            std::string strAddress = rcp.address.toStdString();
            CTxDestination dest = DecodeDestination(strAddress);
            std::string strLabel = rcp.label.toStdString();
            {
                // Check if we have a new address or an updated label
                std::string name;
                if (!m_wallet->getAddress(
                     dest, &name, /*purpose=*/nullptr))
                {
                    m_wallet->setAddressBook(dest, strLabel, wallet::AddressPurpose::SEND);
                }
                else if (name != strLabel)
                {
                    m_wallet->setAddressBook(dest, strLabel, {}); // {} means don't change purpose
                }
            }
        }
        Q_EMIT coinsSent(this, rcp, transaction_array);
    }

    // The transaction is already committed. A later balance arithmetic/read
    // failure must not become a false send failure. Only the guarded poll may
    // publish balances; preserve the last observation and its original age.
    fForceCheckBalanceChanged = true;
    if (m_overview_balances.requireRefresh()) Q_EMIT overviewBalanceObservationChanged();
}

OptionsModel* WalletModel::getOptionsModel() const
{
    return optionsModel;
}

AddressTableModel* WalletModel::getAddressTableModel() const
{
    return addressTableModel;
}

TransactionTableModel* WalletModel::getTransactionTableModel() const
{
    return transactionTableModel;
}

RecentRequestsTableModel* WalletModel::getRecentRequestsTableModel() const
{
    return recentRequestsTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!m_wallet->isCrypted())
    {
        return Unencrypted;
    }
    else if(m_wallet->isLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(const SecureString& passphrase)
{
    return m_wallet->encryptWallet(passphrase);
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if(locked)
    {
        // Lock
        return m_wallet->lock();
    }
    else
    {
        // Unlock
        return m_wallet->unlock(passPhrase);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    m_wallet->lock(); // Make sure wallet is locked before attempting pass change
    return m_wallet->changeWalletPassphrase(oldPass, newPass);
}

// Handlers for core signals
static void NotifyUnload(WalletModel* walletModel)
{
    qDebug() << "NotifyUnload";
    bool invoked = QMetaObject::invokeMethod(walletModel, "unload");
    assert(invoked);
}

static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
    assert(invoked);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel,
        const CTxDestination &address, const std::string &label, bool isMine,
        wallet::AddressPurpose purpose, ChangeType status)
{
    QString strAddress = QString::fromStdString(EncodeDestination(address));
    QString strLabel = QString::fromStdString(label);

    qDebug() << "NotifyAddressBookChanged: " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " purpose=" + QString::number(static_cast<uint8_t>(purpose)) + " status=" + QString::number(status);
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateAddressBook",
                              Q_ARG(QString, strAddress),
                              Q_ARG(QString, strLabel),
                              Q_ARG(bool, isMine),
                              Q_ARG(wallet::AddressPurpose, purpose),
                              Q_ARG(int, status));
    assert(invoked);
}

static void NotifyTransactionChanged(WalletModel *walletmodel, const Txid& hash, ChangeType status)
{
    Q_UNUSED(hash);
    Q_UNUSED(status);
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection);
    assert(invoked);
}

static void ShowProgress(WalletModel *walletmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    bool invoked = QMetaObject::invokeMethod(walletmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
    assert(invoked);
}

static void NotifyCanGetAddressesChanged(WalletModel* walletmodel)
{
    bool invoked = QMetaObject::invokeMethod(walletmodel, "canGetAddressesChanged");
    assert(invoked);
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    m_handler_unload = m_wallet->handleUnload(std::bind_front(&NotifyUnload, this));
    m_handler_status_changed = m_wallet->handleStatusChanged(std::bind_front(&NotifyKeyStoreStatusChanged, this));
    m_handler_address_book_changed = m_wallet->handleAddressBookChanged(std::bind_front(NotifyAddressBookChanged, this));
    m_handler_transaction_changed = m_wallet->handleTransactionChanged(std::bind_front(NotifyTransactionChanged, this));
    m_handler_show_progress = m_wallet->handleShowProgress(std::bind_front(ShowProgress, this));
    m_handler_can_get_addrs_changed = m_wallet->handleCanGetAddressesChanged(std::bind_front(NotifyCanGetAddressesChanged, this));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    m_handler_unload->disconnect();
    m_handler_status_changed->disconnect();
    m_handler_address_book_changed->disconnect();
    m_handler_transaction_changed->disconnect();
    m_handler_show_progress->disconnect();
    m_handler_can_get_addrs_changed->disconnect();
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    const QPointer<WalletModel> alive(this);
    if (QThread::currentThread() != thread()) return UnlockContext(nullptr, false);
    // Bugs in earlier versions may have resulted in wallets with private keys disabled to become "encrypted"
    // (encryption keys are present, but not actually doing anything).
    // To avoid issues with such wallets, check if the wallet has private keys disabled, and if so, return a context
    // that indicates the wallet is not encrypted.
    if (m_wallet->privateKeysDisabled()) {
        return UnlockContext(this, /*valid=*/true);
    }
    bool was_locked = getEncryptionStatus() == Locked;
    // Acquire before any signal can delete this model after unlocking its
    // surviving backend. A failed/cancelled unlock cannot match this scope.
    auto scope = was_locked ? m_wallet->getScopedUnlock() : interfaces::WalletUnlockScope{};
    if(was_locked)
    {
        if (!scope.unlock || !scope.relock) return UnlockContext(this, false);
        // Request UI to unlock wallet
        Q_EMIT requireUnlock(scope.unlock);
        if (!alive) return UnlockContext(nullptr, false, std::move(scope.relock));
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, std::move(scope.relock));
}

WalletModel::UnlockContext::UnlockContext(WalletModel *_wallet, bool _valid, std::function<void()> _relock):
        wallet(_wallet),
        valid(_valid),
        relock(std::move(_relock))
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    // Invalid Qt owner is precisely the case in which native relock must still
    // run. The callback carries only a weak native wallet and authorization id.
    if (relock) relock();
}

bool WalletModel::bumpFee(Txid hash, Txid& new_hash)
{
    const QPointer<WalletModel> alive(this);
    const QPointer<TransactionTableModel> table(transactionTableModel);
    if (QThread::currentThread()!=thread() || !table || table->thread()!=thread()) return false;
    const auto unavailable = [] {
        QMessageBox::critical(nullptr, tr("Fee bump error"),
            tr("Fee amounts unavailable: monetary context or data is missing, invalid, or changed."));
        return false;
    };
    try {
        if (!table->refreshMonetaryContext() || !alive || !table) return alive ? unavailable() : false;
    } catch (const std::exception&) { return alive ? unavailable() : false; }
    if (!alive || !table) return false;
    const pgn::qt::ConsumerMonetarySnapshot snapshot{this,this,table.data(),table->monetaryContext()};
    const auto current = [&] {
        if (!pgn::qt::ConsumerOwnersLive(snapshot) || !alive || !table) return false;
        try {
            if (!table->refreshMonetaryContext() || !alive || !table) return false;
            return pgn::qt::ConsumerSnapshotMatches(snapshot,{this,this,table.data(),table->monetaryContext()});
        } catch (const std::exception&) { return false; }
    };
    if (!current()) return alive ? unavailable() : false;
    CCoinControl coin_control;
    std::vector<bilingual_str> errors;
    CAmount old_fee{-1}; // Invalid sentinels, never fabricated known0.
    CAmount new_fee{-1};
    CMutableTransaction mtx;
    bool created{false};
    try { created=m_wallet->createBumpTransaction(hash, coin_control, errors, old_fee, new_fee, mtx); }
    catch (const std::exception&) { return alive ? unavailable() : false; }
    if (!alive) return false;
    if (!created) {
        QMessageBox::critical(nullptr, tr("Fee bump error"), tr("Increasing transaction fee failed") + "<br />(" +
            (errors.empty() ? QString{} : QString::fromStdString(errors[0].translated).toHtmlEscaped()) +")");
        return false;
    }
    if (!current()) return alive ? unavailable() : false;
    const auto amounts=pgn::qt::PresentBumpFeeAmounts(old_fee,new_fee,snapshot.context);
    if (!amounts) return unavailable();

    // allow a user based fee verification
    /*: Asks a user if they would like to manually increase the fee of a transaction that has already been created. */
    QString questionString = tr("Do you want to increase the fee?");
    questionString.append("<br />");
    questionString.append("<table style=\"text-align: left;\">");
    questionString.append("<tr><td>");
    questionString.append(tr("Current fee:"));
    questionString.append("</td><td>");
    questionString.append(pgn::qt::SendAmountHtml(amounts.texts->old_fee,amounts.texts->scale));
    questionString.append("</td></tr><tr><td>");
    questionString.append(tr("Increase:"));
    questionString.append("</td><td>");
    questionString.append(pgn::qt::SendAmountHtml(amounts.texts->increase,amounts.texts->scale));
    questionString.append("</td></tr><tr><td>");
    questionString.append(tr("New fee:"));
    questionString.append("</td><td>");
    questionString.append(pgn::qt::SendAmountHtml(amounts.texts->new_fee,amounts.texts->scale));
    questionString.append("</td></tr></table>");

    const QPointer<OptionsModel> options(getOptionsModel());
    if (!options || options->thread()!=QThread::currentThread() || !current()) return alive ? unavailable() : false;
    // Display warning in the "Confirm fee bump" window if the "Coin Control Features" option is enabled
    if (options->getCoinControlFeatures()) {
        questionString.append("<br><br>");
        questionString.append(tr("Warning: This may pay the additional fee by reducing change outputs or adding inputs, when necessary. It may add a new change output if one does not already exist. These changes may potentially leak privacy."));
    }

    const bool enable_send{!wallet().privateKeysDisabled() || wallet().hasExternalSigner()};
    const bool always_show_unsigned{options->getEnablePSBTControls()};
    const QPointer<SendConfirmationDialog> confirmationDialog(new SendConfirmationDialog(tr("Confirm fee bump"), questionString, "", "", SEND_CONFIRM_DELAY, enable_send, always_show_unsigned, nullptr));
    if (!confirmationDialog || !current()) {
        if (confirmationDialog) delete confirmationDialog.data();
        return alive ? unavailable() : false;
    }
    confirmationDialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(this,&QObject::destroyed,confirmationDialog.data(),&QDialog::reject,Qt::DirectConnection);
    connect(table.data(),&QObject::destroyed,confirmationDialog.data(),&QDialog::reject,Qt::DirectConnection);
    connect(table.data(),&TransactionTableModel::monetaryContextAboutToChange,confirmationDialog.data(),&QDialog::reject,Qt::DirectConnection);
    // TODO: Replace QDialog::exec() with safer QDialog::show().
    const auto retval = static_cast<QMessageBox::StandardButton>(confirmationDialog->exec());
    if (!current()) return alive ? unavailable() : false;

    // cancel sign&broadcast if user doesn't want to bump the fee
    if (retval != QMessageBox::Yes && retval != QMessageBox::Save) {
        return false;
    }

    // Short-circuit if we are returning a bumped transaction PSBT to clipboard
    if (retval == QMessageBox::Save) {
        // "Create Unsigned" clicked
        PartiallySignedTransaction psbtx(mtx);
        bool complete = false;
        const auto err{wallet().fillPSBT({.sign = false, .bip32_derivs = true}, nullptr, psbtx, complete)};
        if (!current()) return alive ? unavailable() : false;
        if (err || complete) {
            QMessageBox::critical(nullptr, tr("Fee bump error"), tr("Can't draft transaction."));
            return false;
        }
        // Serialize the PSBT
        DataStream ssTx{};
        ssTx << psbtx;
        GUIUtil::setClipboard(EncodeBase64(ssTx.str()).c_str());
        if (alive) Q_EMIT message(tr("PSBT copied"), tr("Fee-bump PSBT copied to clipboard"), CClientUIInterface::MSG_INFORMATION | CClientUIInterface::MODAL);
        return true;
    }

    WalletModel::UnlockContext ctx(requestUnlock());
    if (!alive || !ctx.isValid()) {
        return false;
    }
    if (!current()) return alive ? unavailable() : false;

    assert(!m_wallet->privateKeysDisabled() || wallet().hasExternalSigner());

    // sign bumped transaction
    if (!m_wallet->signBumpTransaction(mtx)) {
        if (!alive) return false;
        QMessageBox::critical(nullptr, tr("Fee bump error"), tr("Can't sign transaction."));
        return false;
    }
    if (!current()) return alive ? unavailable() : false;
    // commit the bumped transaction
    if(!m_wallet->commitBumpTransaction(hash, std::move(mtx), errors, new_hash)) {
        if (!alive) return false;
        QMessageBox::critical(nullptr, tr("Fee bump error"), tr("Could not commit transaction") + "<br />(" +
            (errors.empty() ? QString{} : QString::fromStdString(errors[0].translated).toHtmlEscaped())+")");
        return false;
    }
    return true;
}

void WalletModel::displayAddress(std::string sAddress) const
{
    CTxDestination dest = DecodeDestination(sAddress);
    try {
        util::Result<void> result = m_wallet->displayAddress(dest);
        if (!result) {
            QMessageBox::warning(nullptr, tr("Signer error"), QString::fromStdString(util::ErrorString(result).translated));
        }
    } catch (const std::runtime_error& e) {
        QMessageBox::critical(nullptr, tr("Can't display address"), e.what());
    }
}

bool WalletModel::isWalletEnabled()
{
   return !gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET);
}

QString WalletModel::getWalletName() const
{
    return QString::fromStdString(m_wallet->getWalletName());
}

QString WalletModel::getDisplayName() const
{
    return GUIUtil::WalletDisplayName(getWalletName());
}

bool WalletModel::isMultiwallet() const
{
    return m_node.walletLoader().getWallets().size() > 1;
}

void WalletModel::refresh()
{
    addressTableModel = new AddressTableModel(this);
}

uint256 WalletModel::getLastBlockProcessed() const
{
    return m_client_model ? m_client_model->getBestBlockHash() : uint256{};
}

pgn::qt::CoinControlBalanceResult WalletModel::getCoinControlBalance(const CCoinControl& control)
{
    using S = pgn::qt::CoinControlBalanceStatus;
    if (QThread::currentThread() != thread()) return {S::WrongThread, {}};
    const QPointer<WalletModel> alive(this);
    const QPointer<TransactionTableModel> table(transactionTableModel);
    if (!m_wallet || !table || table->thread() != thread()) return {S::OwnerUnavailable, {}};
    const quint64 request = ++m_coincontrol_request;
    try {
        const CCoinControl copied_control{control}; // Before context callbacks.
        if (!table->refreshMonetaryContext() || !alive || !table) return {S::MissingContext, {}};
        const auto expected = table->monetaryContext();
        return pgn::qt::QueryCoinControlBalance(*alive, expected, copied_control,
            [alive](const CCoinControl& copied) -> std::optional<interfaces::CoinControlBalanceSnapshot> {
                return alive ? alive->wallet().getCoinControlBalance(copied) : std::nullopt;
            },
            [alive, table]() -> std::optional<pgn::core::MonetaryContext> {
                if (!alive || !table || table->thread() != QThread::currentThread() ||
                    !table->refreshMonetaryContext() || !alive || !table) return std::nullopt;
                return table->monetaryContext();
            },
            [alive, table, request] {
                return alive && table && alive->m_coincontrol_request == request &&
                    alive->getTransactionTableModel() == table.data();
            });
    } catch (const std::exception&) {
        return {S::ProviderFailure, {}};
    }
}
