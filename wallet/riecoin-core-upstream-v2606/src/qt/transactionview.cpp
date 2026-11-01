// Copyright (c) 2011-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionview.h>

#include <qt/addresstablemodel.h>
#include <qt/csvmodelwriter.h>
#include <qt/editaddressdialog.h>
#include <qt/guiutil.h>
#include <qt/guiconstants.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactiondescdialog.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactionrecord.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <node/interface_ui.h>

#include <chrono>
#include <limits>
#include <optional>

#include <QApplication>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

TransactionView::TransactionView(const PlatformStyle *platformStyle, QWidget *parent)
    : QWidget(parent), m_platform_style{platformStyle}
{
    // Build filter row
    setContentsMargins(0,0,0,0);

    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->setContentsMargins(0,0,0,0);

    if (platformStyle->getUseExtraSpacing()) {
        hlayout->setSpacing(5);
        hlayout->addSpacing(26);
    } else {
        hlayout->setSpacing(0);
        hlayout->addSpacing(23);
    }

    dateWidget = new QComboBox(this);
    if (platformStyle->getUseExtraSpacing()) {
        dateWidget->setFixedWidth(121);
    } else {
        dateWidget->setFixedWidth(120);
    }
    dateWidget->addItem(tr("All"), All);
    dateWidget->addItem(tr("Today"), Today);
    dateWidget->addItem(tr("This week"), ThisWeek);
    dateWidget->addItem(tr("This month"), ThisMonth);
    dateWidget->addItem(tr("Last month"), LastMonth);
    dateWidget->addItem(tr("This year"), ThisYear);
    dateWidget->addItem(tr("Range…"), Range);
    hlayout->addWidget(dateWidget);

    typeWidget = new QComboBox(this);
    if (platformStyle->getUseExtraSpacing()) {
        typeWidget->setFixedWidth(121);
    } else {
        typeWidget->setFixedWidth(120);
    }

    typeWidget->addItem(tr("All"), TransactionFilterProxy::ALL_TYPES);
    typeWidget->addItem(tr("Received with"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) |
                                        TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
    typeWidget->addItem(tr("Sent to"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) |
                                  TransactionFilterProxy::TYPE(TransactionRecord::SendToOther));
    typeWidget->addItem(tr("Mined"), TransactionFilterProxy::TYPE(TransactionRecord::Generated));
    typeWidget->addItem(tr("Other"), TransactionFilterProxy::TYPE(TransactionRecord::Other));

    hlayout->addWidget(typeWidget);

    search_widget = new QLineEdit(this);
    search_widget->setPlaceholderText(tr("Enter address, transaction id, or label to search"));
    hlayout->addWidget(search_widget);

    amountWidget = new QLineEdit(this);
    // Native parsing must see budget overflow, not a silently truncated prefix.
    // Qt's intrinsic int-sized storage boundary is not a monetary policy.
    amountWidget->setMaxLength(std::numeric_limits<int>::max());
    amountWidget->setPlaceholderText(tr("Min amount"));
    if (platformStyle->getUseExtraSpacing()) {
        amountWidget->setFixedWidth(97);
    } else {
        amountWidget->setFixedWidth(100);
    }
    amountWidget->setEnabled(false);
    amountWidget->setToolTip(tr("Minimum amount filter unavailable until an explicit Core monetary context is supplied."));
    hlayout->addWidget(amountWidget);

    // Delay before filtering transactions
    static constexpr auto input_filter_delay{200ms};

    QTimer* amount_typing_delay = new QTimer(this);
    amount_typing_delay->setSingleShot(true);
    amount_typing_delay->setInterval(input_filter_delay);

    QTimer* prefix_typing_delay = new QTimer(this);
    prefix_typing_delay->setSingleShot(true);
    prefix_typing_delay->setInterval(input_filter_delay);

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(0);

    transactionView = new QTableView(this);
    transactionView->setObjectName("transactionView");
    vlayout->addLayout(hlayout);
    vlayout->addWidget(createDateRangeWidget());
    vlayout->addWidget(transactionView);
    vlayout->setSpacing(0);
    int width = transactionView->verticalScrollBar()->sizeHint().width();
    // Cover scroll bar width with spacing
    if (platformStyle->getUseExtraSpacing()) {
        hlayout->addSpacing(width+2);
    } else {
        hlayout->addSpacing(width);
    }
    transactionView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    transactionView->setTabKeyNavigation(false);
    transactionView->setContextMenuPolicy(Qt::CustomContextMenu);
    transactionView->installEventFilter(this);
    transactionView->setAlternatingRowColors(true);
    transactionView->setSelectionBehavior(QAbstractItemView::SelectRows);
    transactionView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    transactionView->setSortingEnabled(true);
    transactionView->verticalHeader()->hide();

    contextMenu = new QMenu(this);
    contextMenu->setObjectName("contextMenu");
    copyAddressAction = contextMenu->addAction(tr("&Copy address"), this, &TransactionView::copyAddress);
    copyLabelAction = contextMenu->addAction(tr("Copy &label"), this, &TransactionView::copyLabel);
    contextMenu->addAction(tr("Copy &amount"), this, &TransactionView::copyAmount);
    contextMenu->addAction(tr("Copy transaction &ID"), this, &TransactionView::copyTxID);
    contextMenu->addAction(tr("Copy &raw transaction"), this, &TransactionView::copyTxHex);
    contextMenu->addAction(tr("Copy full transaction &details"), this, &TransactionView::copyTxPlainText);
    contextMenu->addAction(tr("&Show transaction details"), this, &TransactionView::showDetails);
    contextMenu->addSeparator();
    bumpFeeAction = contextMenu->addAction(tr("Increase transaction &fee"));
    GUIUtil::ExceptionSafeConnect(bumpFeeAction, &QAction::triggered, this, &TransactionView::bumpFee);
    bumpFeeAction->setObjectName("bumpFeeAction");
    abandonAction = contextMenu->addAction(tr("A&bandon transaction"), this, &TransactionView::abandonTx);
    contextMenu->addAction(tr("&Edit address label"), this, &TransactionView::editLabel);

    connect(dateWidget, qOverload<int>(&QComboBox::activated), this, &TransactionView::chooseDate);
    connect(typeWidget, qOverload<int>(&QComboBox::activated), this, &TransactionView::chooseType);
    connect(amountWidget, &QLineEdit::textChanged, amount_typing_delay, qOverload<>(&QTimer::start));
    connect(amount_typing_delay, &QTimer::timeout, this, &TransactionView::changedAmount);
    connect(search_widget, &QLineEdit::textChanged, prefix_typing_delay, qOverload<>(&QTimer::start));
    connect(prefix_typing_delay, &QTimer::timeout, this, &TransactionView::changedSearch);

    connect(transactionView, &QTableView::doubleClicked, this, &TransactionView::doubleClicked);
    connect(transactionView, &QTableView::customContextMenuRequested, this, &TransactionView::contextualMenu);

    // Double-clicking on a transaction on the transaction history page shows details
    connect(this, &TransactionView::doubleClicked, this, &TransactionView::showDetails);
    // Highlight transaction after fee bump
    connect(this, &TransactionView::bumpedFee, [this](const Txid& txid) {
      focusTransaction(txid);
    });
}

void TransactionView::setModel(WalletModel *_model)
{
    if (thread() != QThread::currentThread() || (_model && (_model->thread() != thread() ||
            !_model->getTransactionTableModel() || _model->getTransactionTableModel()->thread() != thread()))) {
        Q_EMIT message(tr("Binding Failed"), tr("Transaction model ownership is incompatible; no monetary binding was changed."), CClientUIInterface::MSG_ERROR);
        return;
    }
    disconnect(m_context_about_connection);
    disconnect(m_context_changed_connection);
    disconnect(m_context_destroyed_connection);
    clearMinimumAmountBinding();
    transactionView->setModel(nullptr);
    delete transactionProxyModel;
    transactionProxyModel = nullptr;
    this->model = _model;
    if(_model)
    {
        transactionProxyModel = new TransactionFilterProxy(this);
        transactionProxyModel->setSourceModel(_model->getTransactionTableModel());
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setSortRole(Qt::EditRole);
        transactionView->setModel(transactionProxyModel);
        transactionView->sortByColumn(TransactionTableModel::Date, Qt::DescendingOrder);

        auto* table = _model->getTransactionTableModel();
        m_context_about_connection = connect(table, &TransactionTableModel::monetaryContextAboutToChange,
            this, &TransactionView::clearMinimumAmountBinding, Qt::DirectConnection);
        m_context_changed_connection = connect(table, &TransactionTableModel::monetaryContextChanged,
            this, &TransactionView::bindMinimumAmountContext, Qt::DirectConnection);
        m_context_destroyed_connection = connect(table, &QObject::destroyed, this, [this] {
            // Do not ask a dying WalletModel for roles while clearing a filter.
            transactionProxyModel->setSourceModel(nullptr);
            model = nullptr;
            clearMinimumAmountBinding();
        }, Qt::DirectConnection);
        if (refreshTransactionContext()) bindMinimumAmountContext();

        transactionView->resizeColumnToContents(TransactionTableModel::Status);
        transactionView->resizeColumnToContents(TransactionTableModel::Date);
        transactionView->resizeColumnToContents(TransactionTableModel::Type);
        transactionView->resizeColumnToContents(TransactionTableModel::ToAddress);
        transactionView->resizeColumnToContents(TransactionTableModel::Amount);
        transactionView->horizontalHeader()->setStretchLastSection(true);

        if (_model->getOptionsModel())
        {
            // Add third party transaction URLs to context menu
            QStringList listUrls = _model->getOptionsModel()->getThirdPartyTxUrls().split("|", Qt::SkipEmptyParts);
            bool actions_created = false;
            for (int i = 0; i < listUrls.size(); ++i)
            {
                QString url = listUrls[i].trimmed();
                QString host = QUrl(url, QUrl::StrictMode).host();
                if (!host.isEmpty())
                {
                    if (!actions_created) {
                        contextMenu->addSeparator();
                        actions_created = true;
                    }
                    /*: Transactions table context menu action to show the
                        selected transaction in a third-party block explorer.
                        %1 is a stand-in argument for the URL of the explorer. */
                    contextMenu->addAction(tr("Show in %1").arg(host), [this, url] { openThirdPartyTxUrl(url); });
                }
            }
        }
    }
}

void TransactionView::clearMinimumAmountBinding()
{
    // Direct same-thread notification runs BEFORE the model publishes new metadata.
    const QSignalBlocker blocker(amountWidget);
    amountWidget->clear();
    amountWidget->setEnabled(false);
    amountWidget->setStyleSheet(QString{});
    amountWidget->setToolTip(tr("Minimum amount filter unavailable: Core monetary context is missing or invalid."));
    if (transactionProxyModel) transactionProxyModel->setMinAmount(std::nullopt);
    m_minimum_amount.reset();
    m_monetary_binding_token = std::make_shared<const char>(0);
}

void TransactionView::bindMinimumAmountContext()
{
    if (!model || model->thread() != thread() || !model->getTransactionTableModel() ||
            model->getTransactionTableModel()->thread() != thread()) return;
    const auto context = model->getTransactionTableModel()->monetaryContext();
    m_minimum_amount = std::make_unique<pgn::qt::TransactionMinimumAmount>(context);
    if (const auto scale = pgn::qt::TransactionScaleText(context)) {
        amountWidget->setEnabled(true);
        amountWidget->setToolTip(tr("Exact minimum amount (%1). Empty means no amount filter; invalid input keeps the previous filter.").arg(*scale));
    }
}

bool TransactionView::refreshTransactionContext()
{
    return thread() == QThread::currentThread() && model && model->thread() == thread() &&
        model->getTransactionTableModel() && model->getTransactionTableModel()->thread() == thread() &&
        model->getTransactionTableModel()->refreshMonetaryContext();
}

void TransactionView::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
}

void TransactionView::chooseDate(int idx)
{
    if (!transactionProxyModel) return;
    QDate current = QDate::currentDate();
    dateRangeWidget->setVisible(false);
    switch(dateWidget->itemData(idx).toInt())
    {
    case All:
        transactionProxyModel->setDateRange(
                std::nullopt,
                std::nullopt);
        break;
    case Today:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(current),
                std::nullopt);
        break;
    case ThisWeek: {
        // Find last Monday
        QDate startOfWeek = current.addDays(-(current.dayOfWeek()-1));
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(startOfWeek),
                std::nullopt);

        } break;
    case ThisMonth:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(QDate(current.year(), current.month(), 1)),
                std::nullopt);
        break;
    case LastMonth:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(QDate(current.year(), current.month(), 1).addMonths(-1)),
                GUIUtil::StartOfDay(QDate(current.year(), current.month(), 1)));
        break;
    case ThisYear:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(QDate(current.year(), 1, 1)),
                std::nullopt);
        break;
    case Range:
        dateRangeWidget->setVisible(true);
        dateRangeChanged();
        break;
    }
}

void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
}

void TransactionView::changedSearch()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setSearchString(search_widget->text());
}

void TransactionView::changedAmount()
{
    if(!refreshTransactionContext() || !transactionProxyModel || !m_minimum_amount)
        return;
    const auto result = m_minimum_amount->apply(amountWidget->text());
    if (!result) {
        amountWidget->setStyleSheet("QLineEdit { " STYLE_INVALID " }");
        amountWidget->setToolTip(tr("Minimum amount was not applied: input or monetary context is invalid. The previous filter is unchanged."));
        return;
    }
    transactionProxyModel->setMinAmount(result.minimum);
    amountWidget->setStyleSheet(QString{});
    const auto scale = pgn::qt::TransactionScaleText(m_minimum_amount->context());
    amountWidget->setToolTip(tr("Exact minimum amount (%1). Empty means no amount filter; invalid input keeps the previous filter.").arg(scale.value_or(QString{})));
}

void TransactionView::exportClicked()
{
    if (!refreshTransactionContext() || !transactionProxyModel) {
        Q_EMIT message(tr("Exporting Failed"), tr("Transaction history was not exported: the transaction model is unavailable."),
            CClientUIInterface::MSG_ERROR);
        return;
    }

    const auto monetary_context = model->getTransactionTableModel()->monetaryContext();
    if (!monetary_context || pgn::core::ValidateMonetaryContext(*monetary_context) != pgn::core::ExactAmountError::None) {
        Q_EMIT message(tr("Exporting Failed"), tr("Transaction history was not exported: the Core monetary context is unavailable or invalid."),
            CClientUIInterface::MSG_ERROR);
        return;
    }

    const auto binding_token = m_monetary_binding_token;
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Transaction History"), QString(),
        /*: Expanded name of the CSV file format.
            See: https://en.wikipedia.org/wiki/Comma-separated_values. */
        tr("Comma separated file") + QLatin1String(" (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    // The modal filename picker may have processed a metadata/model replacement.
    // Do not publish a file for a silently changed scale or cleared filter.
    if (!refreshTransactionContext() || binding_token != m_monetary_binding_token || !pgn::qt::SameTransactionMonetaryContext(
            monetary_context, model->getTransactionTableModel()->monetaryContext())) {
        Q_EMIT message(tr("Exporting Failed"), tr("Monetary context changed while choosing the file; no export was written. Review the cleared amount filter and retry."), CClientUIInterface::MSG_ERROR);
        return;
    }

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(transactionProxyModel);
    writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
    writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
    writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
    writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
    writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
    // Numeric scale only: no unapproved currency symbol or market value.
    writer.addExactAmountColumn(tr("Amount (10^%1 atoms/unit)").arg(monetary_context->display.decimals),
        0, TransactionTableModel::AmountRole, monetary_context);
    writer.addColumn(tr("ID"), 0, TransactionTableModel::TxHashRole);

    if(!writer.write()) {
        Q_EMIT message(tr("Exporting Failed"), tr("The transaction history was not saved to %1. %2").arg(filename, writer.errorString()),
            CClientUIInterface::MSG_ERROR);
    }
    else {
        Q_EMIT message(tr("Exporting Successful"), tr("The transaction history was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void TransactionView::contextualMenu(const QPoint &point)
{
    QModelIndex index = transactionView->indexAt(point);
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    // If the hash from the TxHashRole (QVariant / QString) is invalid, exit
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    std::optional<Txid> maybeHash = Txid::FromHex(hashQStr.toStdString());
    if (!maybeHash)
        return;

    Txid hash = *maybeHash;
    abandonAction->setEnabled(model->wallet().transactionCanBeAbandoned(hash));
    bumpFeeAction->setEnabled(model->wallet().transactionCanBeBumped(hash));
    copyAddressAction->setEnabled(GUIUtil::hasEntryData(transactionView, 0, TransactionTableModel::AddressRole));
    copyLabelAction->setEnabled(GUIUtil::hasEntryData(transactionView, 0, TransactionTableModel::LabelRole));

    if (index.isValid()) {
        GUIUtil::PopupMenu(contextMenu, transactionView->viewport()->mapToGlobal(point));
    }
}

void TransactionView::abandonTx()
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);

    // get the hash from the TxHashRole (QVariant / QString)
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    Txid hash = Txid::FromHex(hashQStr.toStdString()).value();

    // Abandon the wallet transaction over the walletModel
    model->wallet().abandonTransaction(hash);
}

void TransactionView::bumpFee([[maybe_unused]] bool checked)
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);

    // get the hash from the TxHashRole (QVariant / QString)
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    Txid hash = Txid::FromHex(hashQStr.toStdString()).value();

    // Bump tx fee over the walletModel
    Txid newHash;
    if (model->bumpFee(hash, newHash)) {
        // Update the table
        transactionView->selectionModel()->clearSelection();
        model->getTransactionTableModel()->updateTransaction(hashQStr, CT_UPDATED, true);

        qApp->processEvents();
        Q_EMIT bumpedFee(newHash);
    }
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

void TransactionView::copyAmount()
{
    copyMonetaryData(TransactionTableModel::FormattedAmountRole);
}

void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHashRole);
}

void TransactionView::copyTxHex()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHexRole);
}

void TransactionView::copyTxPlainText()
{
    copyMonetaryData(TransactionTableModel::TxPlainTextRole);
}

bool TransactionView::copyMonetaryData(int role)
{
    if (!refreshTransactionContext()) {
        Q_EMIT message(tr("Copying Failed"), tr("Transaction model or monetary context ownership is unavailable; clipboard unchanged."), CClientUIInterface::MSG_ERROR);
        return false;
    }
    const auto binding_token = m_monetary_binding_token;
    const auto context = model->getTransactionTableModel()->monetaryContext();
    const auto selection = GUIUtil::getEntryData(transactionView, 0);
    if (selection.isEmpty()) {
        Q_EMIT message(tr("Copying Failed"), tr("No transaction is selected; clipboard unchanged."), CClientUIInterface::MSG_ERROR);
        return false;
    }
    const bool details_requested = role == TransactionTableModel::TxPlainTextRole;
    std::optional<QString> details;
    if (details_requested) {
        const auto value = selection.at(0).data(TransactionTableModel::TxPlainTextRole);
        if (value.isValid() && value.metaType().id() == QMetaType::QString) details = value.toString();
    }
    const auto atoms = selection.at(0).data(TransactionTableModel::AmountRole);
    if (binding_token != m_monetary_binding_token || !model || !pgn::qt::SameTransactionMonetaryContext(
            context, model->getTransactionTableModel()->monetaryContext())) {
        Q_EMIT message(tr("Copying Failed"), tr("Monetary binding changed while reading transaction data; clipboard unchanged."), CClientUIInterface::MSG_ERROR);
        return false;
    }
    const auto status = pgn::qt::CopyTransactionMonetaryText(atoms,
        context, details_requested ? pgn::qt::TransactionCopyContent::Details : pgn::qt::TransactionCopyContent::Amount,
        details, GUIUtil::setClipboard);
    if (status != pgn::qt::TransactionAmountStatus::Ready) {
        Q_EMIT message(tr("Copying Failed"), tr("Amount or Core monetary context is unavailable, invalid or unrepresentable; clipboard unchanged."), CClientUIInterface::MSG_ERROR);
        return false;
    }
    return true;
}

void TransactionView::editLabel()
{
    if(!transactionView->selectionModel() ||!model)
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        AddressTableModel *addressBook = model->getAddressTableModel();
        if(!addressBook)
            return;
        QString address = selection.at(0).data(TransactionTableModel::AddressRole).toString();
        if(address.isEmpty())
        {
            // If this transaction has no associated address, exit
            return;
        }
        // Is address in address book? Address book can miss address when a transaction is
        // sent from outside the UI.
        int idx = addressBook->lookupAddress(address);
        if(idx != -1)
        {
            // Edit sending / receiving address
            QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
            // Determine type of address, launch appropriate editor dialog type
            QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

            auto dlg = new EditAddressDialog(
                type == AddressTableModel::Receive
                ? EditAddressDialog::EditReceivingAddress
                : EditAddressDialog::EditSendingAddress, this);
            dlg->setModel(addressBook);
            dlg->loadRow(idx);
            GUIUtil::ShowModalDialogAsynchronously(dlg);
        }
        else
        {
            // Add sending address
            auto dlg = new EditAddressDialog(EditAddressDialog::NewSendingAddress,
                this);
            dlg->setModel(addressBook);
            dlg->setAddress(address);
            GUIUtil::ShowModalDialogAsynchronously(dlg);
        }
    }
}

void TransactionView::showDetails()
{
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        m_opened_dialogs.append(dlg);
        connect(dlg, &QObject::destroyed, [this, dlg] {
            m_opened_dialogs.removeOne(dlg);
        });
        dlg->show();
    }
}

void TransactionView::openThirdPartyTxUrl(QString url)
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
         QDesktopServices::openUrl(QUrl::fromUserInput(url.replace("%s", selection.at(0).data(TransactionTableModel::TxHashRole).toString())));
}

QWidget *TransactionView::createDateRangeWidget()
{
    dateRangeWidget = new QFrame();
    dateRangeWidget->setFrameStyle(static_cast<int>(QFrame::Panel) | static_cast<int>(QFrame::Raised));
    dateRangeWidget->setContentsMargins(1,1,1,1);
    QHBoxLayout *layout = new QHBoxLayout(dateRangeWidget);
    layout->setContentsMargins(0,0,0,0);
    layout->addSpacing(23);
    layout->addWidget(new QLabel(tr("Range:")));

    dateFrom = new QDateTimeEdit(this);
    dateFrom->setDisplayFormat("dd/MM/yy");
    dateFrom->setCalendarPopup(true);
    dateFrom->setMinimumWidth(100);
    dateFrom->setDate(QDate::currentDate().addDays(-7));
    layout->addWidget(dateFrom);
    layout->addWidget(new QLabel(tr("to")));

    dateTo = new QDateTimeEdit(this);
    dateTo->setDisplayFormat("dd/MM/yy");
    dateTo->setCalendarPopup(true);
    dateTo->setMinimumWidth(100);
    dateTo->setDate(QDate::currentDate());
    layout->addWidget(dateTo);
    layout->addStretch();

    // Hide by default
    dateRangeWidget->setVisible(false);

    // Notify on change
    connect(dateFrom, &QDateTimeEdit::dateChanged, this, &TransactionView::dateRangeChanged);
    connect(dateTo, &QDateTimeEdit::dateChanged, this, &TransactionView::dateRangeChanged);

    return dateRangeWidget;
}

void TransactionView::dateRangeChanged()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setDateRange(
            GUIUtil::StartOfDay(dateFrom->date()),
            GUIUtil::StartOfDay(dateTo->date()).addDays(1));
}

void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel)
        return;
    QModelIndex targetIdx = transactionProxyModel->mapFromSource(idx);
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}

void TransactionView::focusTransaction(const Txid& txid)
{
    if (!transactionProxyModel)
        return;

    const QModelIndexList results = this->model->getTransactionTableModel()->match(
        this->model->getTransactionTableModel()->index(0,0),
        TransactionTableModel::TxHashRole,
        QString::fromStdString(txid.ToString()), -1);

    transactionView->setFocus();
    transactionView->selectionModel()->clearSelection();
    for (const QModelIndex& index : results) {
        const QModelIndex targetIndex = transactionProxyModel->mapFromSource(index);
        transactionView->selectionModel()->select(
            targetIndex,
            QItemSelectionModel::Rows | QItemSelectionModel::Select);
        // Called once per destination to ensure all results are in view, unless
        // transactions are not ordered by (ascending or descending) date.
        transactionView->scrollTo(targetIndex);
        // scrollTo() does not scroll far enough the first time when transactions
        // are ordered by ascending date.
        if (index == results[0]) transactionView->scrollTo(targetIndex);
    }
}

// Need to override default Ctrl+C action for amount as default behaviour is just to copy DisplayRole text
bool TransactionView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && ke->modifiers().testFlag(Qt::ControlModifier))
        {
             copyTxPlainText(); // Never fall back to copying DisplayRole on failure.
             return true;
        }
    }
    if (event->type() == QEvent::EnabledChange) {
        if (!isEnabled()) {
            closeOpenedDialogs();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TransactionView::closeOpenedDialogs()
{
    // close all dialogs opened from this view
    for (QDialog* dlg : m_opened_dialogs) {
        dlg->close();
    }
    m_opened_dialogs.clear();
}
