// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING.
#include <qt/pgnsciencepanel.h>

#include <riecoin-build-config.h>
#include <qt/clientmodel.h>
#ifdef ENABLE_WALLET
#include <qt/walletcontroller.h>
#include <qt/walletframe.h>
#include <qt/walletmodel.h>
#endif

#include <QThread>

bool PgnCorePanelCanRequestApproval(const PgnCorePanelPrerequisites& p)
{
    return p.has_client && p.same_node &&
           p.gui_thread && !p.already_attached && p.has_approval && p.has_factory;
}

PgnCorePanelContext::PgnCorePanelContext(QObject* parent) : QObject(parent) {}
PgnCorePanelContext::~PgnCorePanelContext() { detach(); }

bool PgnCorePanelContext::onContextThread() const
{
    return QThread::currentThread() == thread();
}

void PgnCorePanelContext::bind(ClientModel& client, WalletController* controller, WalletFrame* frame)
{
    Q_ASSERT(onContextThread());
    Q_ASSERT(!m_bound);
    m_client = &client;
    m_bound = true;
    connect(&client, &QObject::destroyed, this, &PgnCorePanelContext::detach);
#ifdef ENABLE_WALLET
    m_controller = controller;
    m_frame = frame;
    if (controller) connect(controller, &QObject::destroyed, this, &PgnCorePanelContext::currentWalletChanged);
    if (frame) {
        connect(frame, &QObject::destroyed, this, &PgnCorePanelContext::currentWalletChanged);
        connect(frame, &WalletFrame::currentWalletSet, this, &PgnCorePanelContext::currentWalletChanged);
    }
#else
    Q_ASSERT(!controller && !frame);
#endif
}

bool PgnCorePanelContext::isAttached() const
{
    return onContextThread() && m_bound && m_client;
}

interfaces::Node* PgnCorePanelContext::node() const
{
    return isAttached() ? &m_client->node() : nullptr;
}

ClientModel* PgnCorePanelContext::clientModel() const
{
    return isAttached() ? m_client.data() : nullptr;
}

WalletController* PgnCorePanelContext::walletController() const
{
#ifdef ENABLE_WALLET
    return isAttached() && m_controller && &m_controller->clientModel() == m_client.data() ? m_controller.data() : nullptr;
#else
    return nullptr;
#endif
}

WalletModel* PgnCorePanelContext::currentWalletModel() const
{
#ifdef ENABLE_WALLET
    if (!isAttached() || !walletController() || !m_frame) return nullptr;
    WalletModel* wallet = m_frame->currentWalletModel();
    if (!wallet || &wallet->clientModel() != m_client.data() || &wallet->node() != node()) return nullptr;
    return wallet;
#else
    return nullptr;
#endif
}

void PgnCorePanelContext::detach()
{
    if (!onContextThread()) return;
    if (!m_bound) return;
    // Invalidate BEFORE notification: consumers must never obtain a stale
    // backend through the context while processing this signal.
    m_bound = false;
    if (m_client) disconnect(m_client, nullptr, this, nullptr);
#ifdef ENABLE_WALLET
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    if (m_frame) disconnect(m_frame, nullptr, this, nullptr);
#endif
    m_client.clear();
    m_controller.clear();
    m_frame.clear();
    Q_EMIT aboutToDetach();
}
