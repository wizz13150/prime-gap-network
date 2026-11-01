// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_WALLETAMOUNTBINDING_H
#define PGN_QT_WALLETAMOUNTBINDING_H
#include <QObject>
#include <QPointer>
class WalletModel;
class TransactionTableModel;
class RiecoinAmountField;

/** Real Qt owner/snapshot routing only. No wallet/backend constructed/read.
 * Same-thread aboutToChange revokes input BEFORE table publication; changed
 * binds the copied new snapshot. refresh refuses reentry, missing owner or
 * invalid selected context. This object is not network/profile approval.
 */
class WalletTransferAmountBinding final : public QObject {
    Q_OBJECT
public:
    explicit WalletTransferAmountBinding(RiecoinAmountField&);
    void setWalletModel(WalletModel*);
    bool refresh();
Q_SIGNALS:
    void stateChanged();
private:
    void invalidate();
    bool synchronize();
    RiecoinAmountField& m_field;
    QPointer<WalletModel> m_model;
    QPointer<TransactionTableModel> m_table;
    QMetaObject::Connection m_about, m_changed, m_destroyed, m_table_destroyed;
    bool m_changing{false};
    bool m_syncing{false};
    quint64 m_generation{0};
};
#endif
