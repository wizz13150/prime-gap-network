// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/receiverequestdialog.h>
#include <qt/forms/ui_receiverequestdialog.h>

#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/pgnconsumeramount.h>
#include <qt/qrimagewidget.h>
#include <qt/walletmodel.h>

#include <QDialog>
#include <QString>
#include <QThread>

#include <riecoin-build-config.h> // IWYU pragma: keep

ReceiveRequestDialog::ReceiveRequestDialog(QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags),
      ui(new Ui::ReceiveRequestDialog)
{
    ui->setupUi(this);
    // The designer's historical RIC/zero/URI samples are never observations.
    invalidateOpaqueRequest();
    ui->address_content->clear();
    ui->label_content->clear();
    ui->message_content->clear();
    ui->wallet_content->clear();
    ui->btnCopyAddress->setEnabled(false);
    GUIUtil::handleCloseWindowShortcut(this);
}

ReceiveRequestDialog::~ReceiveRequestDialog()
{
    delete ui;
}

void ReceiveRequestDialog::setModel(WalletModel *_model)
{
    this->model = _model;
}

void ReceiveRequestDialog::setInfo(const SendCoinsRecipient &_info)
{
    this->info = _info;
    setWindowTitle(tr("Request payment to %1").arg(info.label.isEmpty() ? info.address : info.label));
    invalidateOpaqueRequest();
    ui->btnCopyAddress->setEnabled(!info.address.isEmpty());
    ui->address_content->setTextFormat(Qt::PlainText);
    ui->address_content->setText(info.address);
    ui->label_content->setTextFormat(Qt::PlainText);
    ui->message_content->setTextFormat(Qt::PlainText);
    ui->wallet_content->setTextFormat(Qt::PlainText);

    if (!info.label.isEmpty()) {
        ui->label_content->setText(info.label);
    } else {
        ui->label_tag->hide();
        ui->label_content->hide();
    }

    if (!info.message.isEmpty()) {
        ui->message_content->setText(info.message);
    } else {
        ui->message_tag->hide();
        ui->message_content->hide();
    }

    if (model && model->thread()==QThread::currentThread() && !model->getWalletName().isEmpty()) {
        ui->wallet_content->setText(model->getWalletName());
    } else {
        ui->wallet_tag->hide();
        ui->wallet_content->hide();
    }
}

void ReceiveRequestDialog::invalidateOpaqueRequest()
{
    // Recipient v1 has neither optional amount presence nor denomination/
    // context provenance. Preserve the DTO, but do not mint a monetary URI/QR
    // or external-signer request from opaque historical metadata.
    ui->btnCopyURI->setEnabled(false);
    ui->btnSaveAs->setEnabled(false);
    ui->btnSaveAs->hide();
    ui->qr_code->hide();
    ui->uri_content->setTextFormat(Qt::PlainText);
    ui->uri_content->setText(tr("Unavailable (legacy request provenance)"));
    const auto amount=pgn::qt::PresentRequestAmount(pgn::qt::ObserveLegacyRequestAmount(info));
    ui->amount_content->setTextFormat(Qt::PlainText);
    ui->amount_content->setText(amount ? *amount.canonical : tr("Unavailable (legacy amount provenance)"));
    ui->amount_tag->show();
    ui->amount_content->show();
    ui->btnVerify->setEnabled(false);
    ui->btnVerify->hide();
}

void ReceiveRequestDialog::on_btnCopyURI_clicked()
{
    // Explicit refusal, including direct/queued invocation: no URI clipboard
    // sink and no rewriting of the caller's existing clipboard or raw record.
}

void ReceiveRequestDialog::on_btnCopyAddress_clicked()
{
    if (info.address.isEmpty()) return;
    GUIUtil::setClipboard(info.address);
}
