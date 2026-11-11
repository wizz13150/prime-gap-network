#pragma once
#include "social/social_data.h"
namespace pgn::social {
inline QString authStateLabel(AuthState state){
    switch(state){case AuthState::Verified:return "Verified by service";case AuthState::Connecting:return "Signing in";
    case AuthState::RecoveryRequired:return "Recovery required";case AuthState::Revoked:return "Credential revoked";
    case AuthState::Expired:return "Session expired";default:return "Not verified";}
}
inline bool verifiedSession(const AuthSession& s,const Identity& identity){
    bool active=false;for(const auto& device:s.devices)if(device.id==s.device&&!device.revoked)active=true;
    return active&&s.identity==identity&&s.state==AuthState::Verified&&!s.reference.isEmpty()&&s.reference.size()<=256
        &&!s.verificationRef.isEmpty()&&s.verificationRef.size()<=256&&!s.revocationRevision.isEmpty()
        &&s.revocationRevision.size()<=256&&s.expiresUtc>QDateTime::currentDateTimeUtc();
}
inline bool validAuthStatus(const AuthSession& s,const Identity& identity){
    if(s.identity!=identity||s.devices.size()>20||s.availableFlows.size()>5||s.device.size()>256||s.reference.size()>256||s.verificationRef.size()>256||s.revocationRevision.size()>256)return false;
    QSet<QString> ids;for(const auto& d:s.devices){if(d.id.isEmpty()||d.id.size()>256||d.label.size()>80||ids.contains(d.id))return false;ids.insert(d.id);}
    return s.state!=AuthState::Verified||verifiedSession(s,identity);
}
inline QString actionLabel(AccessAction action){
    switch(action){
    case AccessAction::CreateRole:return "Create role";case AccessAction::EditRole:return "Save role";
    case AccessAction::DeleteRole:return "Delete role";case AccessAction::MoveRole:return "Move role";
    case AccessAction::AssignRole:return "Assign role";case AccessAction::RemoveRole:return "Remove role";
    case AccessAction::KickMember:return "Remove member";case AccessAction::BanMember:return "Ban member";
    case AccessAction::SetOverwrite:return "Save channel permissions";case AccessAction::RemoveContent:return "Remove content";
    case AccessAction::PinContent:return "Pin content";
    case AccessAction::ReactContent:return "React";
    }return {};
}
inline QString validateAccess(const AccessSnapshot& s,const Identity& identity){
    if(!validAuthStatus(s.session,identity)||!verifiedSession(s.session,identity))return "Moderator rights require a current session verified by the service.";
    if(!s.authenticated||s.identity!=identity||s.community.isEmpty()||s.community.size()>256||s.revision.isEmpty()||s.revision.size()>256
        ||s.source.trimmed().isEmpty()||s.source.size()>160||!current(s.observedUtc,QDateTime::currentDateTimeUtc()))return "Access snapshot is unavailable, stale or belongs to another identity/network.";
    if(s.roles.size()>100||s.members.size()>200||s.overwrites.size()>500||s.availability.size()>1000||s.permissionCatalog.size()>128)return "Access snapshot exceeds local display limits.";
    QSet<QString> permissions;for(const auto& p:s.permissionCatalog){if(p.isEmpty()||p.size()>80||permissions.contains(p))return "Invalid permission catalog.";permissions.insert(p);}
    auto known=[&](const QStringList& list){if(list.size()>128)return false;for(const auto& p:list)if(!permissions.contains(p))return false;return true;};
    if(!known(s.grantablePermissions))return "Invalid grantable permission list.";
    QSet<QString> roles,members;int moderatorPosition=-1;
    for(const auto& r:s.roles){if(r.id.isEmpty()||r.id.size()>256||roles.contains(r.id)||r.name.trimmed().isEmpty()||r.name.size()>80||r.position<0||r.position>999||!known(r.permissions))return "Invalid role.";roles.insert(r.id);if(r.id==s.moderatorRoleId)moderatorPosition=r.position;}
    if(moderatorPosition<0)return "The service must identify the highest Moderator role.";
    for(const auto& r:s.roles)if(r.position>moderatorPosition)return "A role above Moderator is outside the Social contract.";
    for(const auto& m:s.members){if(m.id.isEmpty()||m.id.size()>256||members.contains(m.id)||m.name.trimmed().isEmpty()||m.name.size()>160||m.roleIds.size()>32)return "Invalid member.";
        for(const auto& role:m.roleIds)if(!roles.contains(role))return "Member references an unknown role.";members.insert(m.id);}
    for(const auto& o:s.overwrites){if(o.channel.isEmpty()||o.channel.size()>256||!(o.member?members:roles).contains(o.target)||!known(o.allow)||!known(o.deny))return "Invalid channel overwrite.";for(const auto& p:o.allow)if(o.deny.contains(p))return "Permission is both allowed and denied.";}
    for(const auto& a:s.availability)if(a.target.size()>256||a.parent.size()>256||a.reaction.size()>80||a.role.size()>256||a.channel.size()>256||a.reason.size()>512)return "Invalid action availability.";
    return {};
}
// A UX preflight, NOT an authorization engine. No role arithmetic or ACL is
// implemented here. The service must reevaluate every mutation on current state.
inline QString unavailable(const AccessSnapshot* snapshot,const AccessRequest& r,bool connected){
    if(!connected)return "No authenticated management service connected.";
    if(!snapshot)return "Authenticated roles and permissions are unavailable.";
    const auto error=validateAccess(*snapshot,r.identity);if(!error.isEmpty())return error;
    if(r.community!=snapshot->community||r.revision!=snapshot->revision)return "Scope or permission revision changed. Refresh first.";
    if(r.sessionReference!=snapshot->session.reference)return "Verified session changed. Sign in again.";
    for(const auto& a:snapshot->availability)if(a.action==r.action&&a.target==r.target&&a.role==r.role&&a.channel==r.channel&&a.scope==r.scope&&a.parent==r.parent&&a.reaction==r.reaction)
        return a.allowed?QString{}:a.reason.isEmpty()?"Permission denied by service.":a.reason;
    return "Permission not granted by the service for this target.";
}
inline bool matchingResult(const AccessResult& result,const AccessRequest& request){
    return result.identity==request.identity&&result.community==request.community&&result.channel==request.channel&&result.requestId==request.requestId;
}
}
