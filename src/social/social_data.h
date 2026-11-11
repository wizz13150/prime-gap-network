#pragma once
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <functional>
#include <optional>
namespace pgn::social {
enum class Section { Chat, Publications, Forum, Miners };
enum class Window { AllTime, Day, Week, Month };
inline QString windowLabel(Window w) {
    switch(w) { case Window::Day:return "Last 24 hours"; case Window::Week:return "Last 7 days";
    case Window::Month:return "Last 30 days"; default:return "All time"; }
}
inline QStringList categories() { return {"General","Mining","Research","Support"}; }
struct Identity {
    QString genesis, subject;
    bool valid() const { return QRegularExpression("^[0-9a-f]{64}$").match(genesis).hasMatch()
        && !subject.trimmed().isEmpty() && subject.size()<=256; }
    bool operator==(const Identity&) const = default;
};
// Read-only display data: no invented channel, member, presence or avatar fetch.
struct Channel { QString id,name,description; };
struct Participant { QString id,name,presence;QStringList roleIds; };
struct AccessRole { QString id,name;int position{};QStringList permissions;bool managed{}; };
struct AccessMember { QString id,name;QStringList roleIds; };
struct ChannelOverwrite { QString channel,target;bool member{};QStringList allow,deny; };
enum class AccessAction { CreateRole,EditRole,DeleteRole,MoveRole,AssignRole,RemoveRole,KickMember,BanMember,SetOverwrite,RemoveContent,PinContent,ReactContent };
enum class AccessScope { Social,Chat,Publications,Forum,Miners };
struct Availability { AccessAction action{};QString target,role,channel;bool allowed{};QString reason;AccessScope scope{AccessScope::Social};QString parent,reaction; };
enum class AuthState { Unverified,Connecting,Verified,RecoveryRequired,Revoked,Expired };
enum class AuthIntent { SignIn,AddDevice,Recover,RevokeDevice,SignOut };
struct AuthDevice { QString id,label;bool revoked{}; };
struct AuthSession {
    Identity identity;AuthState state{AuthState::Unverified};QString reference,verificationRef,revocationRevision,device;
    QDateTime expiresUtc;QVector<AuthDevice> devices;QVector<AuthIntent> availableFlows;
};
struct AuthRequest { Identity identity;AuthIntent intent{};QString device,requestId,sessionReference; };
struct AuthResult { Identity requestedIdentity;QString requestId,reason;AuthSession session; };
struct AccessSnapshot {
    Identity identity;bool authenticated{};QString community,revision,source;QDateTime observedUtc;QString moderatorRoleId;
    QVector<AccessRole> roles;QVector<AccessMember> members;QVector<ChannelOverwrite> overwrites;
    QStringList permissionCatalog,grantablePermissions;QVector<Availability> availability;
    AuthSession session;
};
struct AccessRequest {
    Identity identity;QString community,channel,revision,requestId;
    AccessAction action{};QString target,role,name;int position{};QStringList permissions,allow,deny;QString reason;
    QString sessionReference;
    AccessScope scope{AccessScope::Social};
    QString parent,reaction;
};
enum class AccessOutcome { Accepted,Denied,Conflict,Unavailable };
struct AccessResult {
    Identity identity;QString community,channel,requestId;AccessOutcome outcome{AccessOutcome::Unavailable};
    QString reason,revision;
};
struct Reaction { QString id,label;std::optional<QString> count;bool selected{}; };
struct Contributor { QString id,name; };
struct Reply { QString id, author, body; QDateTime createdUtc;QString authorId;std::optional<QVector<Reaction>> reactions;bool bbcode{}; };
struct Entry {
    QString id, author, title, body;
    QDateTime createdUtc;
    QString category, reference;
    QVector<Reply> replies;
    QString authorId;
    std::optional<QString> replyCount;QDateTime lastActivity;QString state,nextReplyCursor;
    std::optional<QVector<Reaction>> reactions;bool bbcode{};
    std::optional<QVector<Contributor>> contributors;QString lastContributor;
};
struct Provenance { QString genesis, source; QDateTime observedUtc; bool authenticated{}; };
struct Counter { std::optional<QString> value; Provenance provenance; };
struct Activity { std::optional<QDateTime> value; Provenance provenance; };
struct Miner { QString id, name; Counter acceptedGold, validatedAudits; Activity lastActivity;QString socialMemberId; };
struct ReadRequest { Section section; Identity identity; Window window{Window::AllTime}; QString cursor; int limit{100};QString channel; };
struct SendAvailability {bool allowed{};QString reason;};
struct SendRequest {Identity identity;QString channel,requestId,bbcode;};
enum class SendOutcome {Accepted,Rejected,Uncertain};
struct SendResult {Identity identity;QString channel,requestId;SendOutcome outcome{SendOutcome::Uncertain};QString messageId,reason;};
struct Snapshot {
    Identity identity; Section section{}; bool authenticated{};
    QString source; QDateTime observedUtc;
    QVector<Entry> entries; QVector<Miner> miners; Window window{Window::AllTime};
    QString nextCursor; // Opaque UI pagination token; root maps its canonical source.
    QString channel;
    std::optional<QVector<Channel>> channels;
    std::optional<QVector<Participant>> participants;
    std::optional<AccessSnapshot> access;
    std::optional<SendAvailability> chatSend;
};
struct ThreadRequest {Identity identity;QString topic,cursor;int limit{50};};
struct ThreadSnapshot {Identity identity;QString topic,source;bool authenticated{};QDateTime observedUtc;QVector<Reply> replies;QString nextCursor;};
// UI view-model boundary, not a wire protocol. Root supplies canonical contracts
// before any real adapter is connected. The adapter owns authentication, public
// identity/genesis binding, transport, timeout/cancellation and canonical mapping.
// Complete once on the GUI thread, <=100 records/page. No keys or credentials.
// Optional Chat send only. No send is enabled without authenticated availability.
struct Services {
    using Reply=std::function<void(Snapshot,QString)>;
    std::function<void(ReadRequest,Reply)> read;
    // Adapter owns server authorization. Availability is presentation only.
    // A forged client must not bypass the service's current policy evaluation.
    std::function<void(AccessRequest,std::function<void(AccessResult)>)> manage;
    // Root's mature identity-provider adapter performs WebAuthn/OIDC ceremonies
    // and server verification. These structs carry public status/references,
    // never assertions, bearer tokens, recovery codes or private key material.
    std::function<void(AuthRequest,std::function<void(AuthResult)>)> authenticate;
    std::function<void(ThreadRequest,std::function<void(ThreadSnapshot,QString)>)> readThread;
    // requestId is stable across retries of this operation, including lost replies.
    // The adapter/service must enforce idempotence. No optimistic message insertion.
    std::function<void(SendRequest,std::function<void(SendResult)>)> sendChat;
};
inline bool current(const QDateTime& t,const QDateTime& now) {
    return t.isValid()&&t<=now.addSecs(60)&&t>=now.addSecs(-300);
}
inline bool validProvenance(const Provenance& p,const Identity& identity,const QDateTime& now) {
    return p.authenticated&&p.genesis==identity.genesis&&!p.source.trimmed().isEmpty()
        &&p.source.size()<=160&&current(p.observedUtc,now);
}
inline bool validReactions(const std::optional<QVector<Reaction>>& reactions){
    if(!reactions)return true;if(reactions->size()>8)return false;QSet<QString> ids;const QRegularExpression decimal("^(0|[1-9][0-9]{0,79})$");
    for(const auto& r:*reactions){if(r.id.isEmpty()||r.id.size()>80||ids.contains(r.id)||r.label.trimmed().isEmpty()||r.label.size()>40||(r.count&&!decimal.match(*r.count).hasMatch()))return false;ids.insert(r.id);}return true;
}
inline QString validate(const Snapshot& v,const ReadRequest& r,QDateTime now=QDateTime::currentDateTimeUtc()) {
    if(!v.authenticated||!v.identity.valid()||v.identity!=r.identity||v.section!=r.section)
        return "Response does not match the authenticated identity and genesis.";
    if(v.source.trimmed().isEmpty()||v.source.size()>160||!current(v.observedUtc,now))
        return "The response has no current, valid provenance.";
    if(r.limit<1||r.limit>100||v.entries.size()>r.limit||v.miners.size()>r.limit)return "The bounded page limit was exceeded.";
    if(r.cursor.size()>256||v.nextCursor.size()>256||(!v.nextCursor.isEmpty()&&v.nextCursor==r.cursor))
        return "Invalid or repeated pagination token.";
    QSet<QString> ids;const QRegularExpression decimal("^(0|[1-9][0-9]{0,79})$");
    if(r.section==Section::Chat){
        if(v.chatSend&&v.chatSend->reason.size()>256)return "Invalid Chat send availability.";
        if(v.channel.size()>256||(!r.channel.isEmpty()&&v.channel!=r.channel))return "Chat channel does not match the request.";
        if(v.channels){if(v.channels->size()>64)return "Channel directory exceeds its display limit.";
            QSet<QString> channelIds;for(const auto& c:*v.channels){if(c.id.isEmpty()||c.id.size()>256||channelIds.contains(c.id)||c.name.trimmed().isEmpty()||c.name.size()>80||c.description.size()>256)return "Invalid channel directory.";channelIds.insert(c.id);}
            if(!v.channel.isEmpty()&&!channelIds.contains(v.channel))return "Selected channel is absent from the directory.";}
        if(v.participants){if(v.participants->size()>200)return "Participant display limit exceeded.";
            QSet<QString> participantIds;for(const auto& p:*v.participants){if(p.id.isEmpty()||p.id.size()>256||participantIds.contains(p.id)||p.name.trimmed().isEmpty()||p.name.size()>160||p.presence.size()>40||p.roleIds.size()>32)return "Invalid participant directory.";for(const auto& role:p.roleIds)if(role.isEmpty()||role.size()>256)return "Invalid participant role reference.";participantIds.insert(p.id);}}
    }
    if(r.section==Section::Miners) {
        if(!v.entries.isEmpty()||v.window!=r.window)return "Miner observation window does not match the request.";
        for(const auto& m:v.miners) {
            if(m.id.isEmpty()||m.id.size()>256||ids.contains(m.id)||m.name.trimmed().isEmpty()||m.name.size()>160||m.socialMemberId.size()>256)
                return "Invalid or duplicate miner identity.";
            for(const auto* c:{&m.acceptedGold,&m.validatedAudits})
                if(c->value&&(!decimal.match(*c->value).hasMatch()||!validProvenance(c->provenance,r.identity,now)))
                    return "A miner counter lacks valid same-genesis provenance.";
            if(m.lastActivity.value&&(!m.lastActivity.value->isValid()||*m.lastActivity.value>now.addSecs(60)
                ||!validProvenance(m.lastActivity.provenance,r.identity,now)))
                return "Miner last activity lacks valid same-genesis provenance.";
            ids.insert(m.id);
        }
    } else {
        if(!v.miners.isEmpty())return "Unexpected miner data in this section.";
        for(const auto& e:v.entries) {
            if(e.id.isEmpty()||e.id.size()>256||ids.contains(e.id)||e.author.trimmed().isEmpty()
                ||e.author.size()>160||e.title.size()>160||e.body.trimmed().isEmpty()||e.body.size()>16000
                ||e.authorId.size()>256||(r.section==Section::Chat&&e.body.size()>4000)||e.reference.size()>256||!e.createdUtc.isValid()||e.createdUtc>now.addSecs(60))
                return "Invalid or duplicate social record.";
            if(r.section==Section::Forum&&(!categories().contains(e.category)||e.title.trimmed().isEmpty()))
                return "Forum topics require a supported category and title.";
            if(!validReactions(e.reactions)||e.state.size()>40||e.nextReplyCursor.size()>256||(e.lastActivity.isValid()&&e.lastActivity>now.addSecs(60))||(e.replyCount&&!decimal.match(*e.replyCount).hasMatch()))return "Invalid forum metadata or reactions.";
            if(e.lastContributor.size()>160||(e.contributors&&e.contributors->size()>8))return "Invalid forum contributors.";
            if(e.contributors){QSet<QString> contributorIds;for(const auto& c:*e.contributors){if(c.id.isEmpty()||c.id.size()>256||contributorIds.contains(c.id)||c.name.trimmed().isEmpty()||c.name.size()>160)return "Invalid forum contributor.";contributorIds.insert(c.id);}}
            if(r.section!=Section::Forum&&!e.replies.isEmpty())return "Replies belong to forum topics only.";
            if(e.replies.size()>20)return "The 20-reply topic preview limit was exceeded.";
            QSet<QString> replies;
            for(const auto& q:e.replies) {
                if(q.id.isEmpty()||q.id.size()>256||replies.contains(q.id)||q.author.trimmed().isEmpty()
                    ||q.authorId.size()>256||q.author.size()>160||q.body.trimmed().isEmpty()||q.body.size()>4000
                    ||!validReactions(q.reactions)||!q.createdUtc.isValid()||q.createdUtc>now.addSecs(60))return "Invalid or duplicate forum reply.";
                replies.insert(q.id);
            }
            ids.insert(e.id);
        }
    }
    return {};
}
inline QString validateThread(const ThreadSnapshot& v,const ThreadRequest& r,QDateTime now=QDateTime::currentDateTimeUtc()){
    if(!v.authenticated||v.identity!=r.identity||!v.identity.valid()||v.topic!=r.topic||r.topic.isEmpty()||r.topic.size()>256||v.source.trimmed().isEmpty()||v.source.size()>160||!current(v.observedUtc,now))return "Thread response does not match authenticated identity, network and topic.";
    if(r.limit<1||r.limit>50||v.replies.size()>r.limit||r.cursor.size()>256||v.nextCursor.size()>256||(!v.nextCursor.isEmpty()&&v.nextCursor==r.cursor))return "Invalid bounded thread page.";
    QSet<QString> ids;for(const auto& q:v.replies){if(q.id.isEmpty()||q.id.size()>256||ids.contains(q.id)||q.author.trimmed().isEmpty()||q.author.size()>160||q.authorId.size()>256||q.body.trimmed().isEmpty()||q.body.size()>4000||!validReactions(q.reactions)||!q.createdUtc.isValid()||q.createdUtc>now.addSecs(60))return "Invalid thread post.";ids.insert(q.id);}return {};
}
} // namespace pgn::social
