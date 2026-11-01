#pragma once
#include "ui/public_profile_store.h"
#include <QPushButton>
#include <QMenu>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QApplication>
#include <QClipboard>
namespace pgn::ui {
inline QPushButton* createPublicProfileButton(QWidget* parent) {
    auto* button = new QPushButton(parent);
    button->setObjectName("pgn-public-profile");
    auto* menu = new QMenu(button); button->setMenu(menu);
    auto* panel = new QWidget(menu);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14,12,14,12); layout->setSpacing(8);
    layout->addWidget(new QLabel("PUBLIC NAME",panel));
    auto* row = new QHBoxLayout;
    auto* field = new QLineEdit(panel);
    field->setObjectName("pgn-public-profile-name");
    field->setMaxLength(publicNameMaximumLength);
    field->setValidator(new QRegularExpressionValidator(publicNameCharacters(),field));
    auto* save = new QPushButton("Save",panel);
    save->setObjectName("pgn-public-profile-save"); save->setFixedSize(44,24);
    row->addWidget(field,1); row->addWidget(save); layout->addLayout(row);
    auto* hint = new QLabel("Your public name or pseudonym · 48 characters maximum.\nLetters, numbers, spaces and . _ - ' are accepted.",panel);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* anonymous = new QPushButton("Use PGN",panel);
    anonymous->setObjectName("pgn-public-profile-default"); anonymous->setFlat(true);
    anonymous->setMaximumWidth(85); layout->addWidget(anonymous);
    auto* state = new QLabel(panel); state->setObjectName("pgn-public-profile-save-state");
    auto* registration = new QLabel("Network attribution is not registered yet.",panel);
    registration->setObjectName("pgn-public-profile-registration"); registration->setWordWrap(true);
    layout->addWidget(state); layout->addWidget(registration);
    auto* auditLabel=new QLabel("PUBLIC AUDIT ADDRESS",panel);layout->addWidget(auditLabel);
    auto* auditRow=new QHBoxLayout;auto* auditAddress=new QLineEdit(panel);auditAddress->setObjectName("pgn-public-audit-address");auditAddress->setReadOnly(true);auditAddress->setPlaceholderText("Available after wallet setup");
    auto* auditCopy=new QPushButton("Copy",panel);auditCopy->setObjectName("pgn-public-audit-copy");auditCopy->setFixedSize(44,24);auditCopy->setEnabled(false);auditRow->addWidget(auditAddress,1);auditRow->addWidget(auditCopy);layout->addLayout(auditRow);
    auto* auditHint=new QLabel("Share this public address with the testnet coordinator to enable auditing.",panel);auditHint->setWordWrap(true);layout->addWidget(auditHint);panel->setMinimumWidth(310);
    QObject::connect(auditCopy,&QPushButton::clicked,panel,[auditAddress]{if(!auditAddress->text().isEmpty())QApplication::clipboard()->setText(auditAddress->text());});
    auto* embedded = new QWidgetAction(menu); embedded->setDefaultWidget(panel); menu->addAction(embedded); menu->addSeparator();
    QSettings settings;
    const auto initial = settings.value("public/profileDraft",settings.value("public/profileName","PGN")).toString();
    field->setText(initial); // Actual text, never an empty default placeholder.
    const auto refreshHeader = [button] {
        QSettings stored; const auto name = stored.value("public/profileName","PGN").toString();
        const bool chosen = stored.value("public/profileChosen",false).toBool();
        button->setText(chosen ? "PROFILE · " + name : "PROFILE · Choose name");
        button->setProperty("pgn_public_name",name);
        button->setProperty("pgn_public_name_network_confirmed",false);
    };
    const auto persist = [button,field,state,refreshHeader] {
        QSettings stored;
        const bool saved = savePublicProfileDraft(stored,field->text());
        button->setProperty("pgn_public_profile_saved",saved);
        state->setText(saved ? (field->text().trimmed().isEmpty()?QString("Saved · PGN is used while the field is empty"):QString("Saved on this device")) : QString("Not saved · check the name or press Save to retry"));
        state->setStyleSheet(saved ? "color:#69efc5" : "color:#ffbd69");
        refreshHeader();
    };
    QObject::connect(field,&QLineEdit::textChanged,panel,[persist](const QString&){persist();});
    QObject::connect(save,&QPushButton::clicked,panel,persist);
    QObject::connect(field,&QLineEdit::returnPressed,panel,persist);
    QObject::connect(anonymous,&QPushButton::clicked,panel,[field,persist]{field->setText("PGN");persist();});
    if (!settings.contains("public/profileName")) {
        settings.setValue("public/profileName","PGN"); settings.setValue("public/profileDraft","PGN"); settings.setValue("public/profileChosen",false); settings.sync();
    }
    state->setText("Saved on this device"); refreshHeader();
    button->setToolTip("Choose the public name credited with your discoveries. A pseudonym is welcome.");
    return button;
}
inline void presentPublicAuditIdentity(QWidget* button,const QString& address){
    if(!button||!QRegularExpression("^tpgn1[0-9a-z]{8,100}$").match(address).hasMatch())return;
    if(auto* field=button->findChild<QLineEdit*>("pgn-public-audit-address"))field->setText(address);
    if(auto* copy=button->findChild<QPushButton*>("pgn-public-audit-copy"))copy->setEnabled(true);
    button->setProperty("pgn_public_audit_address",address);
}
}
