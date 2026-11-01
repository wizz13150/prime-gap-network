// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING.
#ifndef PGN_QT_SCIENCEPANEL_H
#define PGN_QT_SCIENCEPANEL_H

#include <QObject>
#include <QPointer>

#include <functional>

class BitcoinGUI;
class ClientModel;
class WalletController;
class WalletFrame;
class WalletModel;
class QWidget;
namespace interfaces { class Node; }

/** Borrowed access to the SAME native Core models that BitcoinGUI uses.
 * No node, wallet, keys, monetary state, or consensus authority is created here.
 * Access is valid only on the context's Qt thread, until aboutToDetach.
 */
class PgnCorePanelContext final : public QObject
{
    Q_OBJECT
public:
    explicit PgnCorePanelContext(QObject* parent = nullptr);
    ~PgnCorePanelContext() override;

    bool isAttached() const;
    interfaces::Node* node() const;
    ClientModel* clientModel() const;
    WalletController* walletController() const;
    // nullptr means no native wallet is selected; it is NOT a zero balance.
    WalletModel* currentWalletModel() const;
    void detach();

Q_SIGNALS:
    void aboutToDetach();
    void currentWalletChanged();

private:
    friend class BitcoinGUI;
    // Unit fixture uses genuine cold Core adapters, never a replacement backend.
    friend class PgnCorePanelContractTest;
    void bind(ClientModel& client, WalletController* controller, WalletFrame* frame);
    bool onContextThread() const;
    QPointer<ClientModel> m_client;
    QPointer<WalletController> m_controller;
    QPointer<WalletFrame> m_frame;
    bool m_bound{false};
};

// This is an injection boundary, not an implementation of the PGN approval
// contract. The caller must check the actual initialized PGN Core profile.
// A trusted fork function must check real initialized PGN state/profile and
// remain read-only/no-start (upstream getters themselves are non-const).
// Empty/false approval fails closed. No chain name, RIC default, or placeholder
// consensus value is treated as approval by this frontend.
using PgnCorePanelApproval = std::function<bool(interfaces::Node&)>;
using PgnCorePanelFactory = std::function<QWidget*(PgnCorePanelContext&, QWidget*)>;

// Independently testable prerequisites. Passing these does NOT approve PGN
// consensus; BitcoinGUI must still invoke the explicit approval callback.
struct PgnCorePanelPrerequisites {
    bool has_client{false};
    bool same_node{false};
    bool gui_thread{false};
    bool already_attached{false};
    bool has_approval{false};
    bool has_factory{false};
};
bool PgnCorePanelCanRequestApproval(const PgnCorePanelPrerequisites& prerequisites);

#endif // PGN_QT_SCIENCEPANEL_H
