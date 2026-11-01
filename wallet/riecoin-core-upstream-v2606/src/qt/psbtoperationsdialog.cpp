// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/psbtoperationsdialog.h>
#include <qt/pgnpsbtamountpreview.h>
#include <qt/pgnpsbtworkflow.h>

#include <chainparams.h>
#include <common/messages.h>
#include <core_io.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <node/psbt.h>
#include <node/types.h>
#include <policy/policy.h>
#include <qt/forms/ui_psbtoperationsdialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <util/fs.h>
#include <util/strencodings.h>

#include <fstream>
#include <exception>
#include <iostream>
#include <string>

using common::TransactionErrorString;
using node::AnalyzePSBT;
using node::DEFAULT_MAX_RAW_TX_FEE_RATE;
using node::PSBTAnalysis;
using node::TransactionError;

PSBTOperationsDialog::PSBTOperationsDialog(
    QWidget* parent, WalletModel* wallet_model, ClientModel* client_model) : QDialog(parent, GUIUtil::dialog_flags),
                                                                             m_ui(new Ui::PSBTOperationsDialog),
                                                                             m_wallet_model(wallet_model),
                                                                             m_client_model(client_model)
{
    m_ui->setupUi(this);

    connect(m_ui->signTransactionButton, &QPushButton::clicked, this, &PSBTOperationsDialog::signTransaction);
    connect(m_ui->broadcastTransactionButton, &QPushButton::clicked, this, &PSBTOperationsDialog::broadcastTransaction);
    connect(m_ui->copyToClipboardButton, &QPushButton::clicked, this, &PSBTOperationsDialog::copyToClipboard);
    connect(m_ui->saveButton, &QPushButton::clicked, this, &PSBTOperationsDialog::saveTransaction);

    connect(m_ui->closeButton, &QPushButton::clicked, this, &PSBTOperationsDialog::close);

    m_ui->signTransactionButton->setEnabled(false);
    m_ui->broadcastTransactionButton->setEnabled(false);
}

PSBTOperationsDialog::~PSBTOperationsDialog()
{
    delete m_ui;
}

void PSBTOperationsDialog::openWithPSBT(PartiallySignedTransaction psbtx)
{
    m_transaction_data = psbtx;

    bool complete = FinalizePSBT(psbtx); // Make sure all existing signatures are fully combined before checking for completeness.
    if (m_wallet_model) {
        size_t n_could_sign;
        const auto err{m_wallet_model->wallet().fillPSBT({.sign = false, .bip32_derivs= true}, &n_could_sign, *m_transaction_data, complete)};
        if (err) {
            showStatus(tr("Failed to load transaction: %1")
                           .arg(QString::fromStdString(PSBTErrorString(*err).translated)),
                       StatusLevel::ERR);
            return;
        }
        m_ui->signTransactionButton->setEnabled(!complete && !m_wallet_model->wallet().privateKeysDisabled() && n_could_sign > 0);
    } else {
        m_ui->signTransactionButton->setEnabled(false);
    }

    m_ui->broadcastTransactionButton->setEnabled(complete);

    updateTransactionDisplay();
}

void PSBTOperationsDialog::signTransaction()
{
    bool complete;
    size_t n_signed;

    WalletModel::UnlockContext ctx(m_wallet_model->requestUnlock());

    const auto err{m_wallet_model->wallet().fillPSBT({.sign = true, .bip32_derivs = true}, &n_signed, *m_transaction_data, complete)};

    if (err) {
        showStatus(tr("Failed to sign transaction: %1")
            .arg(QString::fromStdString(PSBTErrorString(*err).translated)), StatusLevel::ERR);
        return;
    }

    const auto availability = updateTransactionDisplay();
    if (pgn::qt::MustDisablePsbtSigning(availability)) {
        // The signatures/PSBT bytes already produced above are retained. Keep
        // the availability notice; do not report a false signing failure or
        // resurrect broadcast/ready text after a local configuration refusal.
        return;
    }
    if (complete && pgn::qt::MustDisablePsbtBroadcast(availability)) {
        showStatus(tr("Signatures were processed, but input amounts remain unavailable. The transaction is not ready to broadcast."), StatusLevel::WARN);
        return;
    }

    if (!complete && !ctx.isValid()) {
        showStatus(tr("Cannot sign inputs while wallet is locked."), StatusLevel::WARN);
    } else if (!complete && n_signed < 1) {
        showStatus(tr("Could not sign any more inputs."), StatusLevel::WARN);
    } else if (!complete) {
        showStatus(tr("Signed %n input(s), but more signatures are still required.", "", n_signed),
            StatusLevel::INFO);
    } else {
        showStatus(tr("Signed transaction successfully. Transaction is ready to broadcast."),
            StatusLevel::INFO);
        m_ui->broadcastTransactionButton->setEnabled(true);
    }
}

void PSBTOperationsDialog::broadcastTransaction()
{
    CMutableTransaction mtx;
    if (!FinalizeAndExtractPSBT(*m_transaction_data, mtx)) {
        // This is never expected to fail unless we were given a malformed PSBT
        // (e.g. with an invalid signature.)
        showStatus(tr("Unknown error processing transaction."), StatusLevel::ERR);
        return;
    }

    CTransactionRef tx = MakeTransactionRef(mtx);
    std::string err_string;
    TransactionError error =
        m_client_model->node().broadcastTransaction(tx, DEFAULT_MAX_RAW_TX_FEE_RATE.GetFeePerK(), err_string);

    if (error == TransactionError::OK) {
        showStatus(tr("Transaction broadcast successfully! Transaction ID: %1")
            .arg(QString::fromStdString(tx->GetHash().GetHex())), StatusLevel::INFO);
    } else {
        showStatus(tr("Transaction broadcast failed: %1")
            .arg(QString::fromStdString(TransactionErrorString(error).translated)), StatusLevel::ERR);
    }
}

void PSBTOperationsDialog::copyToClipboard() {
    DataStream ssTx{};
    ssTx << *m_transaction_data;
    GUIUtil::setClipboard(EncodeBase64(ssTx.str()).c_str());
    showStatus(tr("PSBT copied to clipboard."), StatusLevel::INFO);
}

void PSBTOperationsDialog::saveTransaction() {
    DataStream ssTx{};
    ssTx << *m_transaction_data;

    QString selected_filter;
    // Saving the original bytes must not require or guess a monetary policy.
    // A neutral name avoids a second, formerly fixed8, amount representation.
    const QString filename_suggestion = QStringLiteral("transaction.psbt");
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Save Transaction Data"), filename_suggestion,
        //: Expanded name of the binary PSBT file format. See: BIP 174.
        tr("Partially Signed Transaction (Binary)") + QLatin1String(" (*.psbt)"), &selected_filter);
    if (filename.isEmpty()) {
        return;
    }
    std::ofstream out{filename.toLocal8Bit().data(), std::ofstream::out | std::ofstream::binary};
    out << ssTx.str();
    out.close();
    showStatus(tr("PSBT saved to disk."), StatusLevel::INFO);
}

pgn::qt::PsbtWorkflowAvailability PSBTOperationsDialog::updateTransactionDisplay() {
    if (!m_transaction_data) {
        m_ui->signTransactionButton->setEnabled(false);
        m_ui->broadcastTransactionButton->setEnabled(false);
        const auto notice = pgn::qt::PsbtPreviewUnavailableText(pgn::qt::PsbtPreviewStatus::InvalidStructure);
        m_ui->transactionDescription->setText(notice.toHtmlEscaped());
        showStatus(notice, StatusLevel::ERR);
        return pgn::qt::PsbtWorkflowAvailability::Unavailable;
    }
    // One copied selected context for this entire monetary preview. It is
    // optional metadata, NOT an approval token or authenticated UTXO evidence.
    const auto amounts = [this] {
        try {
            return pgn::qt::BuildPsbtMonetaryPreview(*m_transaction_data, SelectedPgnMonetaryContext());
        } catch (const std::exception&) {
            return pgn::qt::PsbtMonetaryPreview{pgn::qt::PsbtPreviewStatus::InvalidContext, std::nullopt};
        }
    }();
    m_ui->transactionDescription->setText(renderTransaction(*m_transaction_data, amounts));
    // Native AnalyzePSBT independently requires actual consensus rules; display
    // metadata never becomes that domain. Its local-unavailable branch must run
    // before any wallet signability query, including a partial-money preview.
    return showTransactionStatus(*m_transaction_data, amounts);
}

QString PSBTOperationsDialog::renderTransaction(const PartiallySignedTransaction &psbtx,
    const pgn::qt::PsbtMonetaryPreview& amounts)
{
    if (!amounts.readyForDisplay()) return pgn::qt::RenderPsbtMonetaryHtml(amounts, {}, {});
    QStringList destinations;
    std::vector<bool> own_addresses;
    for (const PSBTOutput& out : psbtx.outputs) {
        CTxDestination address;
        ExtractDestination(out.script, address);
        destinations.push_back(QString::fromStdString(EncodeDestination(address)));
        // Check if the address is one of ours
        own_addresses.push_back(m_wallet_model != nullptr && m_wallet_model->wallet().txoutIsMine(CTxOut(out.amount, out.script)));
    }
    QString tx_description = pgn::qt::RenderPsbtMonetaryHtml(amounts, destinations, own_addresses);
    size_t num_unsigned = CountPSBTUnsignedInputs(psbtx);
    if (num_unsigned > 0) {
        tx_description.append("<br><br>");
        tx_description.append(tr("Transaction has %n unsigned input(s).", "", num_unsigned).toHtmlEscaped());
    }

    return tx_description;
}

void PSBTOperationsDialog::showStatus(const QString &msg, StatusLevel level) {
    m_ui->statusBar->setText(msg);
    switch (level) {
        case StatusLevel::INFO: {
            m_ui->statusBar->setStyleSheet("QLabel { background-color : lightgreen }");
            break;
        }
        case StatusLevel::WARN: {
            m_ui->statusBar->setStyleSheet("QLabel { background-color : orange }");
            break;
        }
        case StatusLevel::ERR: {
            m_ui->statusBar->setStyleSheet("QLabel { background-color : red }");
            break;
        }
    }
    m_ui->statusBar->show();
}

size_t PSBTOperationsDialog::couldSignInputs(const PartiallySignedTransaction &psbtx) {
    if (!m_wallet_model) {
        return 0;
    }

    size_t n_signed;
    bool complete;
    const auto err{m_wallet_model->wallet().fillPSBT({.sign = false, .bip32_derivs = false}, &n_signed, *m_transaction_data, complete)};

    if (err) {
        return 0;
    }
    return n_signed;
}

pgn::qt::PsbtWorkflowAvailability PSBTOperationsDialog::showTransactionStatus(
    const PartiallySignedTransaction &psbtx, const pgn::qt::PsbtMonetaryPreview& amounts) {
    PSBTAnalysis analysis = AnalyzePSBT(psbtx);
    const auto availability = pgn::qt::ClassifyPsbtWorkflowAvailability(amounts, analysis);
    if (pgn::qt::MustDisablePsbtSigning(availability)) m_ui->signTransactionButton->setEnabled(false);
    if (pgn::qt::MustDisablePsbtBroadcast(availability)) m_ui->broadcastTransactionButton->setEnabled(false);
    switch (availability) {
        case pgn::qt::PsbtWorkflowAvailability::LocalConfigurationUnavailable:
            showStatus(tr("Local monetary configuration is unavailable. This is not a verdict that the PSBT is invalid."), StatusLevel::WARN);
            return availability;
        case pgn::qt::PsbtWorkflowAvailability::PresentationUnavailable:
            showStatus(pgn::qt::PsbtPreviewUnavailableText(amounts.status), StatusLevel::WARN);
            return availability;
        case pgn::qt::PsbtWorkflowAvailability::InvalidSuppliedData:
            showStatus(tr("Transaction data is inconsistent or outside the configured monetary domain."), StatusLevel::ERR);
            return availability;
        case pgn::qt::PsbtWorkflowAvailability::Unavailable:
            showStatus(tr("PSBT workflow availability could not be established."), StatusLevel::WARN);
            return availability;
        case pgn::qt::PsbtWorkflowAvailability::PreserveExistingPartialSigning:
        case pgn::qt::PsbtWorkflowAvailability::CompletePreviewOnly:
            break;
    }
    // No new enable: partial retains existing Core signing state only, and its
    // broadcast button is disabled above even if a later role is extractable.
    size_t n_could_sign = couldSignInputs(psbtx);

    switch (analysis.next) {
        case PSBTRole::UPDATER: {
            showStatus(tr("Transaction is missing some information about inputs."), StatusLevel::WARN);
            break;
        }
        case PSBTRole::SIGNER: {
            QString need_sig_text = tr("Transaction still needs signature(s).");
            StatusLevel level = StatusLevel::INFO;
            if (!m_wallet_model) {
                need_sig_text += " " + tr("(But no wallet is loaded.)");
                level = StatusLevel::WARN;
            } else if (m_wallet_model->wallet().privateKeysDisabled()) {
                need_sig_text += " " + tr("(But this wallet cannot sign transactions.)");
                level = StatusLevel::WARN;
            } else if (n_could_sign < 1) {
                need_sig_text += " " + tr("(But this wallet does not have the right keys.)"); // XXX wording
                level = StatusLevel::WARN;
            }
            showStatus(need_sig_text, level);
            break;
        }
        case PSBTRole::FINALIZER:
        case PSBTRole::EXTRACTOR: {
            if (availability == pgn::qt::PsbtWorkflowAvailability::PreserveExistingPartialSigning) {
                showStatus(tr("Input amounts remain unavailable. The transaction is not ready to broadcast."), StatusLevel::WARN);
            } else {
                showStatus(tr("Transaction is fully signed and ready for broadcast."), StatusLevel::INFO);
            }
            break;
        }
        default: {
            showStatus(tr("Transaction status is unknown."), StatusLevel::ERR);
            break;
        }
    }
    return availability;
}
