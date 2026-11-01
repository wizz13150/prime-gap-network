// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnwalletamountbinding.h>
#include <qt/pgntransactionamount.h>
#include <qt/riecoinamountfield.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>
#include <QThread>
#include <exception>

WalletTransferAmountBinding::WalletTransferAmountBinding(RiecoinAmountField& field)
    : QObject(&field), m_field(field) {}

void WalletTransferAmountBinding::invalidate()
{
    QPointer<WalletTransferAmountBinding> self(this);
    m_field.clearTransferContext();
    if (self) Q_EMIT stateChanged();
}

void WalletTransferAmountBinding::setWalletModel(WalletModel* model)
{
    if (QThread::currentThread() != thread()) return;
    const auto generation = ++m_generation;
    const QPointer<WalletModel> candidate(model);
    disconnect(m_about); disconnect(m_changed); disconnect(m_destroyed); disconnect(m_table_destroyed);
    m_model.clear(); m_table.clear();
    m_changing = true;
    QPointer<WalletTransferAmountBinding> self(this);
    invalidate();
    if (!self || m_generation != generation) return;
    m_changing = false;
    if (!candidate || candidate->thread() != thread()) return;
    model = candidate.data();
    auto* table = model->getTransactionTableModel();
    if (!table || table->thread() != thread()) return;
    m_model = model; m_table = table;
    m_about = connect(table, &TransactionTableModel::monetaryContextAboutToChange, this, [this] {
        if (QThread::currentThread() != thread()) return;
        m_changing = true;
        invalidate();
    }, Qt::DirectConnection);
    m_changed = connect(table, &TransactionTableModel::monetaryContextChanged, this, [this] {
        if (QThread::currentThread() != thread()) return;
        m_changing = false;
        synchronize();
    }, Qt::DirectConnection);
    m_destroyed = connect(model, &QObject::destroyed, this, [this] {
        if (QThread::currentThread() != thread()) return;
        m_model.clear(); m_table.clear();
        m_changing = true;
        invalidate(); // Never query the dying owner.
    }, Qt::DirectConnection);
    m_table_destroyed = connect(table, &QObject::destroyed, this, [this] {
        if (QThread::currentThread() != thread()) return;
        m_table.clear();
        m_changing = true;
        invalidate();
    }, Qt::DirectConnection);
    refresh();
}

bool WalletTransferAmountBinding::synchronize()
{
    if (m_changing || m_syncing || !m_model || !m_table) return false;
    const auto snapshot = m_table->monetaryContext();
    if (pgn::qt::SameTransactionMonetaryContext(snapshot, m_field.transferContext()))
        return snapshot.has_value();
    m_syncing = true;
    QPointer<WalletTransferAmountBinding> self(this);
    m_field.setTransferContext(*m_model, snapshot);
    if (!self) return false;
    m_syncing = false;
    const bool available = m_field.transferContext().has_value();
    Q_EMIT stateChanged();
    return available;
}

bool WalletTransferAmountBinding::refresh()
{
    if (QThread::currentThread() != thread() || m_changing || m_syncing || !m_model || !m_table) return false;
    if (m_model->thread() != thread() || m_table->thread() != thread()) { invalidate(); return false; }
    QPointer<WalletTransferAmountBinding> self(this);
    try {
        if (!m_table->refreshMonetaryContext()) { invalidate(); return false; }
        if (!self) return false;
        return synchronize();
    } catch (const std::exception&) {
        if (self) invalidate();
        return false;
    }
}
