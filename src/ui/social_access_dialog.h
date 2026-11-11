#pragma once
#include "social/social_access.h"
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
namespace pgn::ui {
class SocialAccessDialog final:public QDialog {
public:
    SocialAccessDialog(social::Identity identity,std::optional<social::AccessSnapshot> snapshot,
        social::Services services,QString channel,QString member,QWidget* parent=nullptr);
    void invalidateAccess(const social::AuthSession& session,QString reason);
private:
    social::Identity identity_;std::optional<social::AccessSnapshot> snapshot_;social::Services services_;QString channel_;
    QListWidget *roles_{},*members_{},*permissions_{},*overwrites_{};
    QLineEdit *name_{},*reason_{};QSpinBox* position_{};QComboBox *memberRole_{},*overwriteRole_{};
    QLabel *status_{},*roleInfo_{},*memberInfo_{};
    QPushButton* createRole_{};
    social::AuthSession auth_;QLabel* authStatus_{};QListWidget* devices_{};
    struct ActionButton{QPushButton* button;social::AccessAction action;};
    QVector<ActionButton> actions_;bool creating_{},busy_{};quint64 authGeneration_{},actionGeneration_{};
    social::AccessRequest request(social::AccessAction action)const;
    void selectRole();void selectMember();void selectOverwrite();void updateActions();void submit(social::AccessAction action);
    void authenticate(social::AuthIntent intent);
};
}
