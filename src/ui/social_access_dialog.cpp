#include "ui/social_access_dialog.h"
#include "ui/social_light_style.h"
#include "ui/social_controls.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QPointer>
#include <QUuid>
#include <algorithm>
#include <memory>
namespace pgn::ui {
namespace {
QLabel* label(QString text){auto* p=new QLabel(text);p->setTextFormat(Qt::PlainText);p->setWordWrap(true);return p;}
QString itemId(QListWidget* list){return list->currentItem()?list->currentItem()->data(Qt::UserRole).toString():QString{};}
QString permissionLabel(QString key){key.replace('_',' ');key=key.toLower();if(!key.isEmpty())key[0]=key[0].toUpper();return key;}
}
SocialAccessDialog::SocialAccessDialog(social::Identity identity,std::optional<social::AccessSnapshot> snapshot,
    social::Services services,QString channel,QString member,QWidget* parent)
    :QDialog(parent),identity_(std::move(identity)),snapshot_(std::move(snapshot)),services_(std::move(services)),channel_(std::move(channel)){
    setObjectName("pgn-social-access-dialog");setProperty("socialSurface",true);setStyleSheet(SocialLightStyle());setWindowTitle("Social · Roles & members");
    setWindowModality(Qt::WindowModal);resize(820,530);setMinimumSize(680,460);
    auto* box=new QVBoxLayout(this);box->setContentsMargins(18,18,18,18);
    auto* heading=label("Roles & members");heading->setProperty("heading",true);box->addWidget(heading);
    box->addWidget(label("Moderator is the highest Social role. Changes require the service to verify current permissions."));
    auto* tabs=new QTabWidget;box->addWidget(tabs,1);
    auth_=snapshot_?snapshot_->session:social::AuthSession{};
    auto* accountPage=new QWidget;auto* accountBox=new QVBoxLayout(accountPage);authStatus_=label(social::authStateLabel(auth_.state));authStatus_->setObjectName("pgn-social-auth-status");accountBox->addWidget(authStatus_);
    accountBox->addWidget(label("A public address or display name cannot recover moderator rights. The identity service verifies possession and account recovery."));
    devices_=new QListWidget;devices_->setAccessibleName("Registered devices reported by service");accountBox->addWidget(devices_,1);
    for(const auto& device:auth_.devices){auto* item=new QListWidgetItem(device.label+(device.revoked?" · revoked":""),devices_);item->setData(Qt::UserRole,device.id);}
    auto* authActions=new QHBoxLayout;
    for(const auto intent:{social::AuthIntent::SignIn,social::AuthIntent::AddDevice,social::AuthIntent::Recover,social::AuthIntent::RevokeDevice,social::AuthIntent::SignOut}){
        const QString text=intent==social::AuthIntent::SignIn?"Sign in":intent==social::AuthIntent::AddDevice?"Add device":intent==social::AuthIntent::Recover?"Recover account":intent==social::AuthIntent::RevokeDevice?"Revoke device":"Sign out";
        auto* button=new QPushButton(text);button->setObjectName("pgn-social-auth-"+QString::number(static_cast<int>(intent)));button->setEnabled(bool(services_.authenticate)&&auth_.availableFlows.contains(intent));authActions->addWidget(button);connect(button,&QPushButton::clicked,this,[this,intent]{authenticate(intent);});}
    accountBox->addLayout(authActions);
    auto button=[&](QHBoxLayout* row,const QString& text,social::AccessAction action,const QString& id){
        auto* p=new QPushButton(text);p->setObjectName(id);row->addWidget(p);actions_.append({p,action});
        connect(p,&QPushButton::clicked,this,[this,action]{submit(action);});return p;};
    auto* rolePage=new QWidget;auto* roleBox=new QHBoxLayout(rolePage);roles_=new QListWidget;roles_->setObjectName("pgn-social-role-list");roles_->setMaximumWidth(210);roleBox->addWidget(roles_);
    auto* edit=new QVBoxLayout;roleBox->addLayout(edit,1);roleInfo_=label("Select a role");edit->addWidget(roleInfo_);
    auto* line=new QHBoxLayout;line->addWidget(label("Name"));name_=new QLineEdit;name_->setMaxLength(80);name_->setAccessibleName("Role name");name_->setObjectName("pgn-social-role-name");line->addWidget(name_,1);
    line->addWidget(label("Position"));position_=new SocialSpinBox;position_->setRange(0,999);position_->setAccessibleName("Role hierarchy position");line->addWidget(position_);edit->addLayout(line);
    permissions_=new QListWidget;permissions_->setObjectName("pgn-social-role-permissions");permissions_->setAccessibleName("Role permissions. Checked permissions are requested, service authorization is required.");edit->addWidget(permissions_,1);
    auto* roleActions=new QHBoxLayout;auto* create=new QPushButton("New role");createRole_=create;create->setObjectName("pgn-social-new-role");roleActions->addWidget(create);
    connect(create,&QPushButton::clicked,this,[this]{creating_=true;roles_->clearSelection();name_->clear();position_->setValue(0);for(int i=0;i<permissions_->count();++i)permissions_->item(i)->setCheckState(Qt::Unchecked);roleInfo_->setText("New role · service decides permitted position and rights");updateActions();name_->setFocus();});
    button(roleActions,"Save",social::AccessAction::EditRole,"pgn-social-save-role");
    button(roleActions,"Delete",social::AccessAction::DeleteRole,"pgn-social-delete-role");
    button(roleActions,"Move",social::AccessAction::MoveRole,"pgn-social-move-role");edit->addLayout(roleActions);tabs->addTab(rolePage,"Roles");
    auto* memberPage=new QWidget;auto* memberBox=new QHBoxLayout(memberPage);members_=new QListWidget;members_->setObjectName("pgn-social-member-list");members_->setMaximumWidth(240);memberBox->addWidget(members_);
    auto* memberEdit=new QVBoxLayout;memberBox->addLayout(memberEdit,1);memberInfo_=label("Select a member");memberEdit->addWidget(memberInfo_);
    memberRole_=new SocialComboBox;memberRole_->setObjectName("pgn-social-member-role");memberRole_->setAccessibleName("Role to assign or remove");memberEdit->addWidget(memberRole_);
    auto* assignment=new QHBoxLayout;button(assignment,"Assign role",social::AccessAction::AssignRole,"pgn-social-assign-role");button(assignment,"Remove role",social::AccessAction::RemoveRole,"pgn-social-remove-role");memberEdit->addLayout(assignment);memberEdit->addStretch();
    reason_=new QLineEdit;reason_->setMaxLength(256);reason_->setPlaceholderText("Reason for moderation");reason_->setAccessibleName("Moderation reason");memberEdit->addWidget(reason_);
    auto* moderation=new QHBoxLayout;button(moderation,"Remove member",social::AccessAction::KickMember,"pgn-social-kick-member");button(moderation,"Ban member",social::AccessAction::BanMember,"pgn-social-ban-member");memberEdit->addLayout(moderation);tabs->addTab(memberPage,"Members");
    auto* overwritePage=new QWidget;auto* overwriteBox=new QVBoxLayout(overwritePage);overwriteBox->addWidget(label(channel_.isEmpty()?"Select a channel to inspect channel permissions.":"Channel: "+channel_+" · Allow / Inherit / Deny"));
    overwriteRole_=new SocialComboBox;overwriteRole_->setAccessibleName("Role or member for channel permission overwrite");overwriteBox->addWidget(overwriteRole_);overwrites_=new QListWidget;overwrites_->setAccessibleName("Channel permissions. Partially checked means inherit.");overwriteBox->addWidget(overwrites_,1);
    auto* overwriteActions=new QHBoxLayout;overwriteActions->addStretch();button(overwriteActions,"Save channel permissions",social::AccessAction::SetOverwrite,"pgn-social-save-overwrite");overwriteBox->addLayout(overwriteActions);tabs->addTab(overwritePage,"Channel permissions");
    tabs->addTab(accountPage,"Account && devices");
    status_=label({});status_->setObjectName("pgn-social-access-status");box->addWidget(status_);auto* bottom=new QHBoxLayout;bottom->addStretch();auto* close=new QPushButton("Close");bottom->addWidget(close);box->addLayout(bottom);connect(close,&QPushButton::clicked,this,&QDialog::close);
    if(snapshot_){const auto error=social::validateAccess(*snapshot_,identity_);if(!error.isEmpty()){status_->setText(error);snapshot_.reset();}}
    if(snapshot_){
        auto sorted=snapshot_->roles;std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.position>b.position;});
        for(const auto& role:sorted){auto* item=new QListWidgetItem(role.name,roles_);item->setData(Qt::UserRole,role.id);memberRole_->addItem(role.name,role.id);overwriteRole_->addItem(role.name,role.id);}
        for(const auto& p:snapshot_->permissionCatalog){auto* item=new QListWidgetItem(permissionLabel(p),permissions_);item->setData(Qt::UserRole,p);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Unchecked);
            auto* overwrite=new QListWidgetItem(permissionLabel(p),overwrites_);overwrite->setData(Qt::UserRole,p);overwrite->setFlags(overwrite->flags()|Qt::ItemIsUserCheckable|Qt::ItemIsUserTristate);overwrite->setCheckState(Qt::PartiallyChecked);}
        for(const auto& person:snapshot_->members){auto* item=new QListWidgetItem(person.name,members_);item->setData(Qt::UserRole,person.id);overwriteRole_->addItem("Member · "+person.name,person.id);overwriteRole_->setItemData(overwriteRole_->count()-1,true,Qt::UserRole+1);}
    }
    connect(roles_,&QListWidget::currentRowChanged,this,[this]{selectRole();});connect(members_,&QListWidget::currentRowChanged,this,[this]{selectMember();});
    connect(memberRole_,&QComboBox::currentIndexChanged,this,[this]{updateActions();});connect(overwriteRole_,&QComboBox::currentIndexChanged,this,[this]{selectOverwrite();});
    roles_->setCurrentRow(0);members_->setCurrentRow(0);selectRole();selectMember();selectOverwrite();
    for(int i=0;i<members_->count();++i)if(members_->item(i)->data(Qt::UserRole)==member){members_->setCurrentRow(i);tabs->setCurrentIndex(1);}
    create->setEnabled(bool(snapshot_));if(!services_.manage)status_->setText("Read only · no authenticated management service connected.");
    updateActions();
}
social::AccessRequest SocialAccessDialog::request(social::AccessAction action)const{
    social::AccessRequest r;r.identity=identity_;r.community=snapshot_?snapshot_->community:QString{};r.revision=snapshot_?snapshot_->revision:QString{};r.action=action;
    r.sessionReference=snapshot_?snapshot_->session.reference:QString{};
    if(action==social::AccessAction::EditRole||action==social::AccessAction::DeleteRole||action==social::AccessAction::MoveRole){r.role=itemId(roles_);if(creating_&&action==social::AccessAction::EditRole){r.action=social::AccessAction::CreateRole;r.role.clear();}
        if(r.action==social::AccessAction::CreateRole||r.action==social::AccessAction::EditRole){r.name=name_->text();for(int i=0;i<permissions_->count();++i)if(permissions_->item(i)->checkState()==Qt::Checked)r.permissions<<permissions_->item(i)->data(Qt::UserRole).toString();}
        if(r.action==social::AccessAction::CreateRole||r.action==social::AccessAction::MoveRole)r.position=position_->value();}
    if(action==social::AccessAction::AssignRole||action==social::AccessAction::RemoveRole){r.target=itemId(members_);r.role=memberRole_->currentData().toString();}
    if(action==social::AccessAction::KickMember||action==social::AccessAction::BanMember){r.target=itemId(members_);r.reason=reason_->text();}
    if(action==social::AccessAction::SetOverwrite){r.channel=channel_;r.target=overwriteRole_->currentData().toString();if(!overwriteRole_->currentData(Qt::UserRole+1).toBool())r.role=r.target;for(int i=0;i<overwrites_->count();++i){auto* p=overwrites_->item(i);if(p->checkState()==Qt::Checked)r.allow<<p->data(Qt::UserRole).toString();else if(p->checkState()==Qt::Unchecked)r.deny<<p->data(Qt::UserRole).toString();}}
    return r;
}
void SocialAccessDialog::invalidateAccess(const social::AuthSession& session,QString reason){
    ++authGeneration_;++actionGeneration_;busy_=false;snapshot_.reset();
    if(social::validAuthStatus(session,identity_)){auth_=session;authStatus_->setText(social::authStateLabel(auth_.state));}
    status_->setText(reason);updateActions();
}
void SocialAccessDialog::authenticate(social::AuthIntent intent){
    if(!services_.authenticate||!auth_.availableFlows.contains(intent)){authStatus_->setText("Unavailable · no identity service flow offered.");return;}
    social::AuthRequest r;r.identity=identity_;r.intent=intent;r.requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);r.sessionReference=auth_.reference;
    if(intent==social::AuthIntent::RevokeDevice){r.device=itemId(devices_);if(r.device.isEmpty()){authStatus_->setText("Select a device to revoke.");return;}}
    authStatus_->setText("Awaiting identity service…");const auto generation=++authGeneration_;++actionGeneration_;busy_=false;snapshot_.reset();updateActions();const QPointer<SocialAccessDialog> weak(this);const auto completed=std::make_shared<bool>(false);
    services_.authenticate(r,[weak,r,generation,completed](social::AuthResult result){if(!weak||*completed||weak->authGeneration_!=generation)return;*completed=true;
        if(result.requestedIdentity!=r.identity||result.requestId!=r.requestId||!social::validAuthStatus(result.session,r.identity)){weak->authStatus_->setText("Denied · identity response mismatch or unverified session.");return;}
        weak->auth_=result.session;weak->snapshot_.reset();weak->updateActions();
        weak->authStatus_->setText(social::authStateLabel(result.session.state)+(result.reason.isEmpty()?QString{}:" · "+result.reason.left(256))+" · refresh roles from service");
        weak->devices_->clear();for(const auto& device:result.session.devices){auto* item=new QListWidgetItem(device.label+(device.revoked?" · revoked":""),weak->devices_);item->setData(Qt::UserRole,device.id);}
    });
}
void SocialAccessDialog::selectRole(){
    creating_=false;if(snapshot_)for(const auto& r:snapshot_->roles)if(r.id==itemId(roles_)){name_->setText(r.name);position_->setValue(r.position);roleInfo_->setText(r.name+" · position "+QString::number(r.position)+(r.managed?" · service managed":""));
        for(int i=0;i<permissions_->count();++i){auto* p=permissions_->item(i);const auto id=p->data(Qt::UserRole).toString();p->setCheckState(r.permissions.contains(id)?Qt::Checked:Qt::Unchecked);
            p->setFlags(snapshot_->grantablePermissions.contains(id)?p->flags()|Qt::ItemIsEnabled:p->flags()&~Qt::ItemIsEnabled);}break;}updateActions();
}
void SocialAccessDialog::selectMember(){
    if(snapshot_)for(const auto& m:snapshot_->members)if(m.id==itemId(members_)){QStringList names;for(const auto& r:snapshot_->roles)if(m.roleIds.contains(r.id))names<<r.name;memberInfo_->setText(m.name+"\n"+m.id+"\nRoles: "+names.join(", "));break;}updateActions();
}
void SocialAccessDialog::selectOverwrite(){
    for(int i=0;i<overwrites_->count();++i)overwrites_->item(i)->setCheckState(Qt::PartiallyChecked);
    if(snapshot_)for(const auto& o:snapshot_->overwrites)if(o.member==overwriteRole_->currentData(Qt::UserRole+1).toBool()&&o.channel==channel_&&o.target==overwriteRole_->currentData().toString())
        for(int i=0;i<overwrites_->count();++i){auto* p=overwrites_->item(i);const auto id=p->data(Qt::UserRole).toString();p->setCheckState(o.allow.contains(id)?Qt::Checked:o.deny.contains(id)?Qt::Unchecked:Qt::PartiallyChecked);}
    updateActions();
}
void SocialAccessDialog::updateActions(){
    const auto snapshot=snapshot_?&*snapshot_:nullptr;const bool canEdit=!busy_&&social::unavailable(snapshot,request(social::AccessAction::EditRole),bool(services_.manage)).isEmpty();
    const bool canMove=!busy_&&social::unavailable(snapshot,request(social::AccessAction::MoveRole),bool(services_.manage)).isEmpty();
    const bool canCreate=!busy_&&social::unavailable(snapshot,request(social::AccessAction::CreateRole),bool(services_.manage)).isEmpty();
    createRole_->setEnabled(canCreate);name_->setEnabled(canEdit);position_->setEnabled(creating_?canCreate:canMove);permissions_->setEnabled(canEdit);
    overwrites_->setEnabled(!busy_&&social::unavailable(snapshot,request(social::AccessAction::SetOverwrite),bool(services_.manage)).isEmpty());
    for(const auto& entry:actions_){const auto r=request(entry.action);const auto error=social::unavailable(snapshot_?&*snapshot_:nullptr,r,bool(services_.manage));
        entry.button->setEnabled(!busy_&&error.isEmpty());entry.button->setToolTip("<qt>"+error.toHtmlEscaped()+"</qt>");}
}
void SocialAccessDialog::submit(social::AccessAction action){
    if(busy_)return;auto r=request(action);const auto error=social::unavailable(snapshot_?&*snapshot_:nullptr,r,bool(services_.manage));
    if(!error.isEmpty()){status_->setText("Denied · "+error);return;}
    if((r.action==social::AccessAction::CreateRole||r.action==social::AccessAction::EditRole)&&r.name.trimmed().isEmpty()){status_->setText("A role name is required.");return;}
    if(snapshot_)for(const auto& permission:r.permissions)if(!snapshot_->grantablePermissions.contains(permission)&&
        std::none_of(snapshot_->roles.begin(),snapshot_->roles.end(),[&](const auto& role){return role.id==r.role&&role.permissions.contains(permission);})){status_->setText("Denied · permission not offered by service.");return;}
    r.requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);busy_=true;const auto generation=++actionGeneration_;status_->setText("Awaiting service authorization…");updateActions();const QPointer<SocialAccessDialog> weak(this);const auto completed=std::make_shared<bool>(false);
    services_.manage(r,[weak,r,generation,completed](social::AccessResult result){if(!weak||*completed||weak->actionGeneration_!=generation)return;*completed=true;weak->busy_=false;
        if(!social::matchingResult(result,r)){weak->status_->setText("Denied · response scope does not match this request.");weak->updateActions();return;}
        const auto state=result.outcome==social::AccessOutcome::Accepted?"Accepted by service · refresh to read updated state":result.outcome==social::AccessOutcome::Denied?"Denied":result.outcome==social::AccessOutcome::Conflict?"Permissions changed · refresh required":"Service unavailable";
        weak->status_->setText(QString(state)+(result.reason.isEmpty()?QString{}:" · "+result.reason.left(512)));weak->updateActions();
        if(result.outcome==social::AccessOutcome::Accepted||result.outcome==social::AccessOutcome::Conflict){weak->snapshot_.reset();weak->updateActions();}
    });
}
}
