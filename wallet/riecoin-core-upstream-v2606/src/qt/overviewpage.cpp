// Copyright (c) 2011-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactionoverviewwidget.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <qt/walletframe.h>

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QDateTime>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStatusTipEvent>
#include <QThread>
#include <QAbstractProxyModel>
#include <qt/pgnconsumeramount.h>

#include <algorithm>
#include <map>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle* _platformStyle, QObject* parent = nullptr)
        : QAbstractItemDelegate(parent), platformStyle(_platformStyle)
    {
        connect(this, &TxViewDelegate::width_changed, this, &TxViewDelegate::sizeHintChanged);
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const override
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        const QVariant raw_amount=index.data(TransactionTableModel::AmountRole);
        const QAbstractItemModel* source=index.model();
        while (const auto* proxy=qobject_cast<const QAbstractProxyModel*>(source)) source=proxy->sourceModel();
        const auto* table=qobject_cast<const TransactionTableModel*>(source);
        const auto context=table ? table->monetaryContext() : std::nullopt;
        const auto amount=pgn::qt::PresentConsumerTransactionAtoms(raw_amount,context,true,!index.data(TransactionTableModel::ConfirmedRole).toBool());
        const auto scale=pgn::qt::TransactionScaleText(context);
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft | Qt::AlignVCenter, address, &boundingRect);

        if(amount && *amount.atoms < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText=tr("Unavailable");
        if (amount && scale) amountText=*amount.display+QStringLiteral(" (")+*scale+QLatin1Char(')');
        QRect amount_bounding_rect;
        painter->drawText(amountRect, Qt::AlignRight | Qt::AlignVCenter, amountText, &amount_bounding_rect);

        painter->setPen(option.palette.color(QPalette::Text));
        QRect date_bounding_rect;
        painter->drawText(amountRect, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::dateTimeStr(date), &date_bounding_rect);

        // 0.4*date_bounding_rect.width() is used to visually distinguish a date from an amount.
        const int minimum_width = 1.4 * date_bounding_rect.width() + amount_bounding_rect.width();
        const auto search = m_minimum_width.find(index.row());
        if (search == m_minimum_width.end() || search->second != minimum_width) {
            m_minimum_width[index.row()] = minimum_width;
            Q_EMIT width_changed(index);
        }

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto search = m_minimum_width.find(index.row());
        const int minimum_text_width = search == m_minimum_width.end() ? 0 : search->second;
        return {DECORATION_SIZE + 8 + minimum_text_width, DECORATION_SIZE};
    }

Q_SIGNALS:
    //! An intermediate signal for emitting from the `paint() const` member function.
    void width_changed(const QModelIndex& index) const;

private:
    const PlatformStyle* platformStyle;
    mutable std::map<int, int> m_minimum_width;
};

#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent, bool closed) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    m_platform_style{platformStyle},
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);
    refreshBalancePresentation(); // No wallet observation/policy: never .ui sample amounts.

    // Show Version.
    ui->version->setText(QString::fromStdString(FormatFullVersion()));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(platformStyle, this);
    transactionView->setModel(walletModel);

    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons())
        exportButton->setIcon(platformStyle->SingleColorIcon(":/icons/export"));
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    ui->listTransactions->setLayout(vbox);

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, &QPushButton::clicked, transactionView, &TransactionView::exportClicked);
}

void OverviewPage::setPrivacy(bool privacy)
{
    m_privacy = privacy;
    if (clientModel && clientModel->getOptionsModel())
        clientModel->getOptionsModel()->setOption(OptionsModel::OptionID::MaskValues, privacy);
    refreshBalancePresentation();

    ui->listTransactions->setVisible(!m_privacy);

    const QString status_tip = m_privacy ? tr("Privacy mode activated for the Overview tab. To unmask the values, uncheck Settings->Mask values.") : "";
    setStatusTip(status_tip);
    QStatusTipEvent event(status_tip);
    QApplication::sendEvent(this, &event);
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const interfaces::WalletBalances& balances)
{
    Q_UNUSED(balances);
    // A DTO/default cache or direct slot invocation is not read availability.
    // Only the dedicated successful-read observation can supply these labels.
    refreshBalancePresentation();
}

void OverviewPage::refreshBalancePresentation()
{
    if (thread() != QThread::currentThread()) return;
    if (m_privacy) {
        applyBalancePresentation(pgn::qt::PresentOverviewBalances(std::nullopt, std::nullopt, true));
        return; // Do not even inspect balance data/availability while masked.
    }
    if (m_balance_context_changing || !walletModel || walletModel->thread() != thread() ||
        !walletModel->getTransactionTableModel() || walletModel->getTransactionTableModel()->thread() != thread()) {
        applyBalancePresentation(pgn::qt::PresentOverviewBalances(std::nullopt, std::nullopt, false));
        return;
    }
    applyBalancePresentation(pgn::qt::PresentOverviewBalances(walletModel->getOverviewBalanceObservation(),
        walletModel->getTransactionTableModel()->monetaryContext(), false));
}

void OverviewPage::applyBalancePresentation(const pgn::qt::OverviewBalancePresentation& presentation)
{
    const std::array<QLabel*, 4> values{ui->labelBalance, ui->labelUnconfirmed, ui->labelImmature, ui->labelTotal};
    const std::array<QLabel*, 4> headings{ui->labelBalanceText, ui->labelPendingText, ui->labelImmatureText, ui->labelTotalText};
    const std::array<QString, 4> names{tr("Available"), tr("Pending"), tr("Immature"), tr("Total")};
    const bool hidden = presentation.status == pgn::qt::OverviewBalanceStatus::Hidden;
    const bool ready = static_cast<bool>(presentation);
    const bool stale = presentation.status == pgn::qt::OverviewBalanceStatus::LastObserved;
    const QString placeholder = hidden ? tr("Hidden") : tr("Unavailable");
    const QString tooltip = ready
        ? (stale ? tr("Last successful wallet balance observation: %1. Refresh required; these are not newly read balances.")
                 : tr("Wallet balance observation: %1. This timestamp is an observation, not network or backend readiness."))
            .arg(presentation.observed_at->toString(Qt::ISODateWithMs))
        : hidden ? tr("Balance values and observation metadata are hidden.")
                 : tr("Balance unavailable: a genuine read, matching explicit monetary context, valid amounts and exact representable total are required.");
    for (size_t i = 0; i != values.size(); ++i) {
        values[i]->setTextFormat(Qt::PlainText);
        values[i]->setText(ready ? (*presentation.amounts)[i] : placeholder);
        values[i]->setToolTip(tooltip);
        headings[i]->setText(ready ? (stale ? tr("%1 (last observed; %2)") : tr("%1 (%2)"))
            .arg(names[i], *presentation.scale) : names[i]);
    }
    // Unknown and masked states use fixed layout, not a zero/nonzero side channel.
    const bool show_immature = !ready || *presentation.immature_nonzero;
    ui->labelImmature->setVisible(show_immature);
    ui->labelImmatureText->setVisible(show_immature);
    setProperty("pgnBalanceStatus", static_cast<int>(presentation.status));
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if (model) {
        // Show warning, for example if this is a prerelease version
        connect(model, &ClientModel::alertsChanged, this, &OverviewPage::updateAlerts);
        updateAlerts(model->getStatusBarWarnings());

        connect(model->getOptionsModel(), &OptionsModel::fontForMoneyChanged, this, &OverviewPage::setMonospacedFont);
        setMonospacedFont(clientModel->getOptionsModel()->getFontForMoney());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    if (thread() != QThread::currentThread()) return;
    for (const auto& connection : m_balance_connections) disconnect(connection);
    m_balance_connections.clear();
    m_balance_context_changing = false;
    if (model && (model->thread() != thread() || !model->getTransactionTableModel() ||
        model->getTransactionTableModel()->thread() != thread())) model = nullptr;
    this->walletModel = model;
    // Publish unavailability before binding another owner; never reuse old values.
    applyBalancePresentation(pgn::qt::PresentOverviewBalances(std::nullopt, std::nullopt, m_privacy));
    transactionView->setModel(model);
    if (model) {
        auto* table = model->getTransactionTableModel();
        m_balance_connections.push_back(connect(model, &WalletModel::overviewBalanceObservationChanged,
            this, &OverviewPage::refreshBalancePresentation, Qt::DirectConnection));
        m_balance_connections.push_back(connect(table, &TransactionTableModel::monetaryContextAboutToChange, this, [this] {
            m_balance_context_changing = true;
            refreshBalancePresentation();
        }, Qt::DirectConnection));
        m_balance_connections.push_back(connect(table, &TransactionTableModel::monetaryContextChanged, this, [this] {
            m_balance_context_changing = false;
            refreshBalancePresentation();
        }, Qt::DirectConnection));
        const auto owner_destroyed = [this] {
            walletModel = nullptr;
            m_balance_context_changing = false;
            refreshBalancePresentation(); // No method call on a dying wallet/model.
        };
        m_balance_connections.push_back(connect(model, &QObject::destroyed, this, owner_destroyed, Qt::DirectConnection));
        m_balance_connections.push_back(connect(table, &QObject::destroyed, this, owner_destroyed, Qt::DirectConnection));
    }
    refreshBalancePresentation();
}

void OverviewPage::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
}

// Only show most recent NUM_ITEMS rows
void OverviewPage::LimitTransactionRows()
{
    if (filter && ui->listTransactions && ui->listTransactions->model() && filter.get() == ui->listTransactions->model()) {
        for (int i = 0; i < filter->rowCount(); ++i) {
            ui->listTransactions->setRowHidden(i, i >= NUM_ITEMS);
        }
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelSyncWarning->setVisible(fShow);
}

void OverviewPage::setMonospacedFont(const QFont& f)
{
    ui->labelBalance->setFont(f);
    ui->labelUnconfirmed->setFont(f);
    ui->labelImmature->setFont(f);
    ui->labelTotal->setFont(f);
}
