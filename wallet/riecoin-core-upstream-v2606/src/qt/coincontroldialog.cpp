// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/coincontroldialog.h>
#include <qt/forms/ui_coincontroldialog.h>

#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/pgncoincontrolpreview.h>
#include <qt/pgncoincontrollabels.h>
#include <qt/pgnsendamountdisplay.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <key_io.h>
#include <policy/policy.h>
#include <wallet/coincontrol.h>
#include <wallet/coinselection.h>
#include <wallet/wallet.h>

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QThread>
#include <util/overflow.h>
#include <limits>

using wallet::CCoinControl;

QList<CAmount> CoinControlDialog::payAmounts;
bool CoinControlDialog::fSubtractFeeFromAmount = false;

bool CCoinControlWidgetItem::operator<(const QTreeWidgetItem &other) const {
    int column = treeWidget()->sortColumn();
    if (column == CoinControlDialog::COLUMN_AMOUNT)
        return pgn::qt::CoinControlAtomsLess(data(column,Qt::UserRole),other.data(column,Qt::UserRole));
    if (column == CoinControlDialog::COLUMN_DATE || column == CoinControlDialog::COLUMN_CONFIRMATIONS)
        return data(column, Qt::UserRole).toLongLong() < other.data(column, Qt::UserRole).toLongLong();
    return QTreeWidgetItem::operator<(other);
}

CoinControlDialog::CoinControlDialog(CCoinControl& coin_control, WalletModel* _model, const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::CoinControlDialog),
    m_coin_control(coin_control),
    model(_model),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(tr("&Copy address"), this, &CoinControlDialog::copyAddress);
    contextMenu->addAction(tr("Copy &label"), this, &CoinControlDialog::copyLabel);
    contextMenu->addAction(tr("Copy &amount"), this, &CoinControlDialog::copyAmount);
    m_copy_transaction_outpoint_action = contextMenu->addAction(tr("Copy transaction &ID and output index"), this, &CoinControlDialog::copyTransactionOutpoint);
    contextMenu->addSeparator();
    lockAction = contextMenu->addAction(tr("L&ock unspent"), this, &CoinControlDialog::lockCoin);
    unlockAction = contextMenu->addAction(tr("&Unlock unspent"), this, &CoinControlDialog::unlockCoin);
    connect(ui->treeWidget, &QWidget::customContextMenuRequested, this, &CoinControlDialog::showMenu);

    // clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);

    connect(clipboardQuantityAction, &QAction::triggered, this, &CoinControlDialog::clipboardQuantity);
    connect(clipboardAmountAction, &QAction::triggered, this, &CoinControlDialog::clipboardAmount);
    connect(clipboardFeeAction, &QAction::triggered, this, &CoinControlDialog::clipboardFee);
    connect(clipboardAfterFeeAction, &QAction::triggered, this, &CoinControlDialog::clipboardAfterFee);
    connect(clipboardBytesAction, &QAction::triggered, this, &CoinControlDialog::clipboardBytes);
    connect(clipboardChangeAction, &QAction::triggered, this, &CoinControlDialog::clipboardChange);

    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    // toggle tree/list mode
    connect(ui->radioTreeMode, &QRadioButton::toggled, this, &CoinControlDialog::radioTreeMode);
    connect(ui->radioListMode, &QRadioButton::toggled, this, &CoinControlDialog::radioListMode);

    // click on checkbox
    connect(ui->treeWidget, &QTreeWidget::itemChanged, this, &CoinControlDialog::viewItemChanged);

    // click on header
    ui->treeWidget->header()->setSectionsClickable(true);
    connect(ui->treeWidget->header(), &QHeaderView::sectionClicked, this, &CoinControlDialog::headerSectionClicked);

    // ok button
    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &CoinControlDialog::buttonBoxClicked);

    // (un)select all
    connect(ui->pushButtonSelectAll, &QPushButton::clicked, this, &CoinControlDialog::buttonSelectAllClicked);

    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 84);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 110);
    ui->treeWidget->setColumnWidth(COLUMN_LABEL, 190);
    ui->treeWidget->setColumnWidth(COLUMN_ADDRESS, 320);
    ui->treeWidget->setColumnWidth(COLUMN_DATE, 130);
    ui->treeWidget->setColumnWidth(COLUMN_CONFIRMATIONS, 110);

    // default view is sorted by amount desc
    sortView(COLUMN_AMOUNT, Qt::DescendingOrder);

    // restore list mode and sortorder as a convenience feature
    QSettings settings;
    if (settings.contains("nCoinControlMode") && !settings.value("nCoinControlMode").toBool())
        ui->radioTreeMode->click();
    if (settings.contains("nCoinControlSortColumn") && settings.contains("nCoinControlSortOrder"))
        sortView(settings.value("nCoinControlSortColumn").toInt(), (static_cast<Qt::SortOrder>(settings.value("nCoinControlSortOrder").toInt())));

    GUIUtil::handleCloseWindowShortcut(this);

    invalidateMonetary(); // Designer defaults are never observed balances/fees.
    if (_model && _model->thread()==thread() && _model->getTransactionTableModel()) {
        auto* table=_model->getTransactionTableModel();
        connect(table,&TransactionTableModel::monetaryContextAboutToChange,this,&CoinControlDialog::invalidateMonetary,Qt::DirectConnection);
        connect(table,&TransactionTableModel::monetaryContextChanged,this,&CoinControlDialog::refreshViewAmounts,Qt::DirectConnection);
        connect(_model,&QObject::destroyed,this,&CoinControlDialog::invalidateMonetary);
    }
    if(_model && _model->getOptionsModel() && _model->getAddressTableModel())
    {
        updateView();
        updateLabelLocked();
        CoinControlDialog::updateLabels(m_coin_control, _model, this);
    }
}

CoinControlDialog::~CoinControlDialog()
{
    QSettings settings;
    settings.setValue("nCoinControlMode", ui->radioListMode->isChecked());
    settings.setValue("nCoinControlSortColumn", sortColumn);
    settings.setValue("nCoinControlSortOrder", (int)sortOrder);

    delete ui;
}

// ok button
void CoinControlDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
        done(QDialog::Accepted); // closes the dialog
}

// (un)select all
void CoinControlDialog::buttonSelectAllClicked()
{
    Qt::CheckState state = Qt::Checked;
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
    {
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != Qt::Unchecked)
        {
            state = Qt::Unchecked;
            break;
        }
    }
    ui->treeWidget->setEnabled(false);
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != state)
                ui->treeWidget->topLevelItem(i)->setCheckState(COLUMN_CHECKBOX, state);
    ui->treeWidget->setEnabled(true);
    if (state == Qt::Unchecked)
        m_coin_control.UnSelectAll(); // just to be sure
    CoinControlDialog::updateLabels(m_coin_control, model, this);
}

// context menu
void CoinControlDialog::showMenu(const QPoint &point)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if(item)
    {
        contextMenuItem = item;

        // disable some items (like Copy Transaction ID, lock, unlock) for tree roots in context menu
        auto txid{Txid::FromHex(item->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString())};
        if (txid) { // a valid txid means this is a child node, and not a parent node in tree mode
            m_copy_transaction_outpoint_action->setEnabled(true);
            if (model->wallet().isLockedCoin(COutPoint(*txid, item->data(COLUMN_ADDRESS, VOutRole).toUInt()))) {
                lockAction->setEnabled(false);
                unlockAction->setEnabled(true);
            } else {
                lockAction->setEnabled(true);
                unlockAction->setEnabled(false);
            }
        } else { // this means click on parent node in tree mode -> disable all
            m_copy_transaction_outpoint_action->setEnabled(false);
            lockAction->setEnabled(false);
            unlockAction->setEnabled(false);
        }

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy amount
void CoinControlDialog::copyAmount()
{
    const auto context=currentContext();
    if (!contextMenuItem || !pgn::qt::SameTransactionMonetaryContext(context,m_display_context)) return;
    const auto text=pgn::qt::CoinControlAmountText(contextMenuItem->data(COLUMN_AMOUNT,Qt::UserRole),context,contextMenuItem->parent() || !ui->radioTreeMode->isChecked());
    if (text) GUIUtil::setClipboard(*text);
}

// context menu action: copy label
void CoinControlDialog::copyLabel()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_LABEL).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_LABEL));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_LABEL));
}

// context menu action: copy address
void CoinControlDialog::copyAddress()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_ADDRESS).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_ADDRESS));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_ADDRESS));
}

// context menu action: copy transaction id and vout index
void CoinControlDialog::copyTransactionOutpoint()
{
    const QString address = contextMenuItem->data(COLUMN_ADDRESS, TxHashRole).toString();
    const QString vout = contextMenuItem->data(COLUMN_ADDRESS, VOutRole).toString();
    const QString outpoint = QString("%1:%2").arg(address).arg(vout);

    GUIUtil::setClipboard(outpoint);
}

// context menu action: lock coin
void CoinControlDialog::lockCoin()
{
    if (contextMenuItem->checkState(COLUMN_CHECKBOX) == Qt::Checked)
        contextMenuItem->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

    COutPoint outpt(Txid::FromHex(contextMenuItem->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString()).value(), contextMenuItem->data(COLUMN_ADDRESS, VOutRole).toUInt());
    model->wallet().lockCoin(outpt, /* write_to_db = */ true);
    contextMenuItem->setDisabled(true);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, platformStyle->SingleColorIcon(":/icons/lock_closed"));
    updateLabelLocked();
}

// context menu action: unlock coin
void CoinControlDialog::unlockCoin()
{
    COutPoint outpt(Txid::FromHex(contextMenuItem->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString()).value(), contextMenuItem->data(COLUMN_ADDRESS, VOutRole).toUInt());
    model->wallet().unlockCoin(outpt);
    contextMenuItem->setDisabled(false);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, QIcon());
    updateLabelLocked();
}

// copy label "Quantity" to clipboard
void CoinControlDialog::clipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// copy label "Amount" to clipboard
void CoinControlDialog::clipboardAmount()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlAmount,currentContext(),GUIUtil::setClipboard);
}

// copy label "Fee" to clipboard
void CoinControlDialog::clipboardFee()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlFee,currentContext(),GUIUtil::setClipboard);
}

// copy label "After fee" to clipboard
void CoinControlDialog::clipboardAfterFee()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlAfterFee,currentContext(),GUIUtil::setClipboard);
}

// copy label "Bytes" to clipboard
void CoinControlDialog::clipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// copy label "Change" to clipboard
void CoinControlDialog::clipboardChange()
{
    pgn::qt::CopyCoinControlLabelAmount(*ui->labelCoinControlChange,currentContext(),GUIUtil::setClipboard);
}

// treeview: sort
void CoinControlDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
}

// treeview: clicked on header
void CoinControlDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    }
    else
    {
        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else
        {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_LABEL || sortColumn == COLUMN_ADDRESS) ? Qt::AscendingOrder : Qt::DescendingOrder); // if label or address then default => asc, else default => desc
        }

        sortView(sortColumn, sortOrder);
    }
}

// toggle tree mode
void CoinControlDialog::radioTreeMode(bool checked)
{
    if (checked && model)
        updateView();
}

// toggle list mode
void CoinControlDialog::radioListMode(bool checked)
{
    if (checked && model)
        updateView();
}

// checkbox clicked by user
void CoinControlDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
    if (column != COLUMN_CHECKBOX) return;
    auto txid{Txid::FromHex(item->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString())};
    if (txid) { // a valid txid means this is a child node, and not a parent node in tree mode
        COutPoint outpt(*txid, item->data(COLUMN_ADDRESS, VOutRole).toUInt());

        if (item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked)
            m_coin_control.UnSelect(outpt);
        else if (item->isDisabled()) // locked (this happens if "check all" through parent node)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        else
            m_coin_control.Select(outpt);

        // selection changed -> update labels
        if (ui->treeWidget->isEnabled()) // do not update on every click for (un)select all
            CoinControlDialog::updateLabels(m_coin_control, model, this);
    }
}

// shows count of locked unspent outputs
void CoinControlDialog::updateLabelLocked()
{
    if (!model) return;
    std::vector<COutPoint> vOutpts;
    model->wallet().listLockedCoins(vOutpts);
    if (vOutpts.size() > 0)
    {
       ui->labelLocked->setText(tr("(%1 locked)").arg(vOutpts.size()));
       ui->labelLocked->setVisible(true);
    }
    else ui->labelLocked->setVisible(false);
}

void CoinControlDialog::updateLabels(CCoinControl& coin_control, WalletModel* model, QDialog* dialog)
{
    const QPointer<QDialog> alive(dialog);
    if (!alive) return;
    pgn::qt::InvalidateCoinControlLabels(*dialog);
    const QPointer<WalletModel> owner(model);
    if (!alive || !owner || QThread::currentThread()!=dialog->thread() || owner->thread()!=dialog->thread()) return;
    const QPointer<TransactionTableModel> table(owner->getTransactionTableModel());
    if (!table) return;
    const auto context=table->monetaryContext(); // One copied context for this preview.
    const auto current=[&] {
        return alive && owner && table && owner->getTransactionTableModel()==table &&
            pgn::qt::SameTransactionMonetaryContext(context,table->monetaryContext());
    };
    if (!pgn::qt::SendAmountDisplay(context).available()) return;
    const CCoinControl query(coin_control); // Rendering never cleans/mutates real selections.
    std::vector<pgn::qt::CoinControlPreviewInput> inputs;
    const std::vector<CAmount> payments(payAmounts.cbegin(),payAmounts.cend());
    const bool subtract=fSubtractFeeFromAmount;
    try {
        const auto selected=query.ListSelected();
        const auto coins=owner->wallet().getCoins(selected);
        if (!current() || coins.size()!=selected.size()) return;
        for (const auto& out:coins) {
            if (out.depth_in_main_chain<0 || out.is_spent) return; // Unknown/spent != omitted/zero.
            uint32_t input_bytes{};
            int version{};
            std::vector<unsigned char> program;
            const bool witness=out.txout.scriptPubKey.IsWitnessProgram(version,program);
            if (witness) {
                // Retained preliminary key-path heuristic, NEVER final weight.
                if (version==0 && program.size()==20) input_bytes=41+107/WITNESS_SCALE_FACTOR;
                else if (version==1 && program.size()==32) input_bytes=41+66/WITNESS_SCALE_FACTOR;
                else return; // Unknown script size is unavailable, not an invented148-byte input.
            } else {
                CTxDestination destination;
                if (!ExtractDestination(out.txout.scriptPubKey,destination)) return;
                const auto* hash=std::get_if<PKHash>(&destination);
                CPubKey pubkey;
                if (!hash || !owner->wallet().getPubKey(out.txout.scriptPubKey,ToKeyID(*hash),pubkey) || !current()) return;
                input_bytes=pubkey.IsCompressed()?148:180;
            }
            inputs.push_back({out.txout.nValue,input_bytes,witness});
        }
        const auto result=pgn::qt::PresentCoinControlPreview(inputs,payments,subtract,context,
            [&](uint32_t bytes)->std::optional<CAmount> {
                if (!current()) return std::nullopt;
                const CAmount fee=owner->wallet().getMinimumFee(bytes,query,nullptr,nullptr);
                return current()?std::optional<CAmount>{fee}:std::nullopt;
            });
        if (!current()) return;
        pgn::qt::PublishCoinControlLabels(*dialog,result,context);
    } catch (...) {
        // Typed native local-unavailable/errors and source read failures cannot
        // leave an old plausible fee/afterfee/change or become a default0.
        if (alive) pgn::qt::InvalidateCoinControlLabels(*dialog);
    }
}

void CoinControlDialog::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        updateView();
    }

    QDialog::changeEvent(e);
}

void CoinControlDialog::updateView()
{
    if (!model || !model->getOptionsModel() || !model->getAddressTableModel())
        { invalidateMonetary(); return; }

    const auto context=currentContext();
    if (!pgn::qt::SendAmountDisplay(context).available()) { invalidateMonetary(); return; }
    interfaces::Wallet::CoinsList all_coins;
    try { all_coins=model->wallet().listCoins(); }
    catch (...) { invalidateMonetary(); return; }
    if (!model || !pgn::qt::SameTransactionMonetaryContext(context,currentContext())) { invalidateMonetary(); return; }
    const QPointer<CoinControlDialog> alive(this);
    const QPointer<QTreeWidget> tree(ui->treeWidget);
    const bool was_blocked=tree->blockSignals(true); // Painting checkboxes never changes real selections.

    bool treeMode = ui->radioTreeMode->isChecked();

    contextMenuItem=nullptr;
    ui->treeWidget->clear();
    ui->treeWidget->setEnabled(false); // performance, otherwise updateLabels would be called for every checked checkbox
    ui->treeWidget->setAlternatingRowColors(!treeMode);
    QFlags<Qt::ItemFlag> flgCheckbox = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    QFlags<Qt::ItemFlag> flgTristate = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate;

    try {
    for (const auto& coins : all_coins) {
        CCoinControlWidgetItem* itemWalletAddress{nullptr};
        QString sWalletAddress = QString::fromStdString(EncodeDestination(coins.first));
        QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);
        if (sWalletLabel.isEmpty())
            sWalletLabel = tr("(no label)");

        if (treeMode)
        {
            // wallet address
            itemWalletAddress = new CCoinControlWidgetItem(ui->treeWidget);

            itemWalletAddress->setFlags(flgTristate);
            itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

            // label
            itemWalletAddress->setText(COLUMN_LABEL, sWalletLabel);

            // address
            itemWalletAddress->setText(COLUMN_ADDRESS, sWalletAddress);
        }

        std::optional<CAmount> nSum{0};
        int nChildren = 0;
        for (const auto& outpair : coins.second) {
            const COutPoint& output = std::get<0>(outpair);
            const interfaces::WalletTxOut& out = std::get<1>(outpair);
            if (out.txout.nValue<0 || out.txout.nValue>context->transfer.max_atomic) nSum.reset();
            else if (nSum) nSum=CheckedAdd(*nSum,out.txout.nValue);
            nChildren++;

            CCoinControlWidgetItem *itemOutput;
            if (treeMode)    itemOutput = new CCoinControlWidgetItem(itemWalletAddress);
            else             itemOutput = new CCoinControlWidgetItem(ui->treeWidget);
            itemOutput->setFlags(flgCheckbox);
            itemOutput->setCheckState(COLUMN_CHECKBOX,Qt::Unchecked);

            // address
            CTxDestination outputAddress;
            QString sAddress = "";
            if(ExtractDestination(out.txout.scriptPubKey, outputAddress))
            {
                sAddress = QString::fromStdString(EncodeDestination(outputAddress));

                // if listMode or change => show Riecoin address. In tree mode, address is not shown again for direct wallet address outputs
                if (!treeMode || (!(sAddress == sWalletAddress)))
                    itemOutput->setText(COLUMN_ADDRESS, sAddress);
            }

            // label
            if (!(sAddress == sWalletAddress)) // change
            {
                // tooltip from where the change comes from
                itemOutput->setToolTip(COLUMN_LABEL, tr("change from %1 (%2)").arg(sWalletLabel).arg(sWalletAddress));
                itemOutput->setText(COLUMN_LABEL, tr("(change)"));
            }
            else if (!treeMode)
            {
                QString sLabel = model->getAddressTableModel()->labelForAddress(sAddress);
                if (sLabel.isEmpty())
                    sLabel = tr("(no label)");
                itemOutput->setText(COLUMN_LABEL, sLabel);
            }

            // amount
            itemOutput->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)out.txout.nValue)); // padding so that sorting works correctly
            const auto output_text=pgn::qt::CoinControlAmountText(itemOutput->data(COLUMN_AMOUNT,Qt::UserRole),context,true);
            itemOutput->setText(COLUMN_AMOUNT,output_text?*output_text:tr("Unavailable"));

            // date
            itemOutput->setText(COLUMN_DATE, GUIUtil::dateTimeStr(out.time));
            itemOutput->setData(COLUMN_DATE, Qt::UserRole, QVariant((qlonglong)out.time));

            // confirmations
            itemOutput->setText(COLUMN_CONFIRMATIONS, QString::number(out.depth_in_main_chain));
            itemOutput->setData(COLUMN_CONFIRMATIONS, Qt::UserRole, QVariant((qlonglong)out.depth_in_main_chain));

            // transaction hash
            itemOutput->setData(COLUMN_ADDRESS, TxHashRole, QString::fromStdString(output.hash.GetHex()));

            // vout index
            itemOutput->setData(COLUMN_ADDRESS, VOutRole, output.n);

             // disable locked coins
            if (model->wallet().isLockedCoin(output))
            {
                itemOutput->setDisabled(true);
                itemOutput->setIcon(COLUMN_CHECKBOX, platformStyle->SingleColorIcon(":/icons/lock_closed"));
            }

            // set checkbox
            if (m_coin_control.IsSelected(output))
                itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
        }

        // amount
        if (treeMode)
        {
            itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(nChildren) + ")");
            itemWalletAddress->setData(COLUMN_AMOUNT,Qt::UserRole,nSum?QVariant::fromValue<qint64>(*nSum):QVariant{});
            const auto sum_text=pgn::qt::CoinControlAmountText(itemWalletAddress->data(COLUMN_AMOUNT,Qt::UserRole),context,false);
            itemWalletAddress->setText(COLUMN_AMOUNT,sum_text?*sum_text:tr("Unavailable"));
        }
    }
    } catch (...) {
        if (tree) tree->blockSignals(was_blocked);
        if (alive) { ui->treeWidget->setEnabled(true); invalidateMonetary(); }
        return;
    }

    // expand all partially selected
    if (treeMode)
    {
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
                ui->treeWidget->topLevelItem(i)->setExpanded(true);
    }

    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);
    if (!alive) return;
    if (tree) tree->blockSignals(was_blocked);
    if (!pgn::qt::SameTransactionMonetaryContext(context,currentContext())) { invalidateMonetary(); return; }
    refreshViewAmounts();
}

std::optional<pgn::core::MonetaryContext> CoinControlDialog::currentContext() const
{
    if (QThread::currentThread()!=thread() || !model || model->thread()!=thread()) return std::nullopt;
    const auto* table=model->getTransactionTableModel();
    return table && table->thread()==thread()?table->monetaryContext():std::nullopt;
}
void CoinControlDialog::invalidateMonetary()
{
    m_display_context.reset();
    pgn::qt::InvalidateCoinControlLabels(*this);
    for (QTreeWidgetItemIterator i(ui->treeWidget);*i;++i) (*i)->setText(COLUMN_AMOUNT,tr("Unavailable"));
}
void CoinControlDialog::refreshViewAmounts()
{
    m_display_context.reset();
    const auto context=currentContext();
    const auto scale=pgn::qt::SendAmountDisplay(context).scale();
    ui->treeWidget->headerItem()->setText(COLUMN_AMOUNT,scale?tr("Amount (%1)").arg(*scale):tr("Amount (unavailable)"));
    for (QTreeWidgetItemIterator i(ui->treeWidget);*i;++i) {
        const bool individual=(*i)->parent() || !ui->radioTreeMode->isChecked();
        const auto text=pgn::qt::CoinControlAmountText((*i)->data(COLUMN_AMOUNT,Qt::UserRole),context,individual);
        (*i)->setText(COLUMN_AMOUNT,text?*text:tr("Unavailable"));
    }
    if (!pgn::qt::SameTransactionMonetaryContext(context,currentContext())) { invalidateMonetary(); return; }
    m_display_context=context;
    CoinControlDialog::updateLabels(m_coin_control,model,this);
}
