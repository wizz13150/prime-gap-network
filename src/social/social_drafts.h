#pragma once
#include "social/social_data.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <array>
namespace pgn::social {
struct Draft { QString title,body,reference,category{"General"}; };
struct ChatOperation {QString requestId,sourceHash;};
inline bool validOperation(const QString& channel,const ChatOperation& op){return !channel.isEmpty()&&channel.size()<=256&&QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$").match(op.requestId).hasMatch()&&QRegularExpression("^[0-9a-f]{64}$").match(op.sourceHash).hasMatch();}
struct Drafts { std::array<Draft,3> sections; QMap<QString,QString> replies,chats,replyTargets;QMap<QString,ChatOperation> chatOperations; };
inline constexpr int draftLimit(int i){return i==0?4000:i==1?12000:16000;}
class DraftStore {
    QString base_;
public:
    explicit DraftStore(QString base):base_(std::move(base)){}
    QString path(const Identity& identity)const{
        const auto scope=identity.genesis.toUtf8()+'\0'+identity.subject.toUtf8();
        return QFileInfo(base_).absolutePath()+"/scoped-drafts/"
            +QString::fromLatin1(QCryptographicHash::hash(scope,QCryptographicHash::Sha256).toHex())+".json";
    }
    QString load(const Identity& identity,Drafts& output)const{
        if(!identity.valid())return "Select a public identity and network before loading drafts.";
        QFile file(path(identity));if(!file.exists()){output={};return {};}
        if(!file.open(QIODevice::ReadOnly))return "Scoped drafts could not be opened.";
        if(file.size()>524288)return "Scoped drafts exceed the supported file size.";
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject())return "Scoped drafts are not valid JSON.";
        const auto obj=doc.object();
        if(obj.value("version").toInt()!=2||obj.value("genesis").toString()!=identity.genesis
            ||obj.value("subject").toString()!=identity.subject)return "Draft scope or format does not match this identity and genesis.";
        Drafts candidate;
        for(int i=0;i<3;++i){
            if(!obj.value(QString::number(i)).isObject())return "A scoped draft is missing.";
            const auto d=obj.value(QString::number(i)).toObject();
            for(const auto& field:{"title","body","reference","category"})
                if(!d.value(field).isString())return "A scoped draft has invalid fields.";
            candidate.sections[i]={d.value("title").toString(),d.value("body").toString(),d.value("reference").toString(),d.value("category").toString()};
            const auto& v=candidate.sections[i];
            if(v.title.size()>160||v.body.size()>draftLimit(i)||v.reference.size()>256
                ||!categories().contains(v.category))return "A scoped draft exceeds its supported limits.";
        }
        if(!obj.value("replies").isObject())return "Reply drafts are invalid.";
        const auto replies=obj.value("replies").toObject();
        if(replies.size()>8)return "The eight-reply draft limit was exceeded.";
        for(auto it=replies.begin();it!=replies.end();++it){
            if(it.key().isEmpty()||it.key().size()>256||!it.value().isString()||it.value().toString().size()>4000)
                return "A reply draft is invalid.";
            candidate.replies.insert(it.key(),it.value().toString());
        }
        if(obj.contains("chats")){
            if(!obj.value("chats").isObject()||obj.value("chats").toObject().size()>16)return "Channel drafts exceed their limit.";
            const auto chats=obj.value("chats").toObject();for(auto it=chats.begin();it!=chats.end();++it){
                if(it.key().isEmpty()||it.key().size()>256||!it.value().isString()||it.value().toString().size()>4000)return "Invalid channel draft.";
                candidate.chats.insert(it.key(),it.value().toString());}
        }
        if(obj.contains("replyTargets")){if(!obj.value("replyTargets").isObject())return "Invalid reply targets.";const auto targets=obj.value("replyTargets").toObject();if(targets.size()>8)return "Too many reply targets.";for(auto it=targets.begin();it!=targets.end();++it){if(!candidate.replies.contains(it.key())||!it.value().isString()||it.value().toString().isEmpty()||it.value().toString().size()>256)return "Invalid reply target.";candidate.replyTargets.insert(it.key(),it.value().toString());}}
        if(obj.contains("chatOperations")){if(!obj.value("chatOperations").isObject()||obj.value("chatOperations").toObject().size()>16)return "Invalid pending Chat operations.";const auto operations=obj.value("chatOperations").toObject();for(auto it=operations.begin();it!=operations.end();++it){if(!it.value().isObject())return "Invalid pending Chat operation.";const auto value=it.value().toObject();ChatOperation op{value.value("requestId").toString(),value.value("sourceHash").toString()};if(!validOperation(it.key(),op))return "Invalid pending Chat operation.";candidate.chatOperations.insert(it.key(),op);}}
        output=std::move(candidate);return {};
    }
    QString save(const Identity& identity,const Drafts& drafts)const{
        if(!identity.valid())return "Select a public identity and network before saving drafts.";
        QJsonObject obj{{"version",2},{"genesis",identity.genesis},{"subject",identity.subject}};
        for(int i=0;i<3;++i){
            const auto& d=drafts.sections[i];
            if(d.title.size()>160||d.body.size()>draftLimit(i)||d.reference.size()>256
                ||!categories().contains(d.category))return "A draft exceeds its supported limits.";
            obj.insert(QString::number(i),QJsonObject{{"title",d.title},{"body",d.body},{"reference",d.reference},{"category",d.category}});
        }
        if(drafts.replies.size()>8)return "Keep at most eight reply drafts per identity and network.";
        QJsonObject replies;
        for(auto it=drafts.replies.begin();it!=drafts.replies.end();++it){
            if(it.key().isEmpty()||it.key().size()>256||it.value().size()>4000)return "A reply draft exceeds its supported limits.";
            replies.insert(it.key(),it.value());
        }
        obj.insert("replies",replies);
        if(drafts.chats.size()>16)return "Keep at most sixteen channel drafts per identity and network.";
        QJsonObject chats;for(auto it=drafts.chats.begin();it!=drafts.chats.end();++it){
            if(it.key().isEmpty()||it.key().size()>256||it.value().size()>4000)return "Invalid channel draft.";chats.insert(it.key(),it.value());}
        obj.insert("chats",chats);
        if(drafts.replyTargets.size()>8)return "Too many reply targets.";QJsonObject targets;for(auto it=drafts.replyTargets.begin();it!=drafts.replyTargets.end();++it){if(!drafts.replies.contains(it.key())||it.value().isEmpty()||it.value().size()>256)return "Invalid reply target.";targets.insert(it.key(),it.value());}obj.insert("replyTargets",targets);
        const auto destination=path(identity);QDir dir(QFileInfo(destination).absolutePath());
        if(drafts.chatOperations.size()>16)return "Keep at most sixteen pending Chat operations.";
        QJsonObject operations;for(auto it=drafts.chatOperations.begin();it!=drafts.chatOperations.end();++it){if(!validOperation(it.key(),it.value()))return "Invalid pending Chat operation.";operations.insert(it.key(),QJsonObject{{"requestId",it.value().requestId},{"sourceHash",it.value().sourceHash}});}obj.insert("chatOperations",operations);
        if(!dir.mkpath("."))return "The scoped drafts directory could not be created.";
        if(!QFile::exists(destination)&&dir.entryList({"*.json"},QDir::Files).size()>=64)
            return "The 64-scope local draft limit was reached. Existing drafts are preserved.";
        const auto bytes=QJsonDocument(obj).toJson(QJsonDocument::Compact);
        if(bytes.size()>524288)return "Scoped drafts exceed the supported file size.";
        QSaveFile file(destination);
        if(!file.open(QIODevice::WriteOnly))return "Scoped drafts could not be saved.";
        file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        if(file.write(bytes)!=bytes.size()||!file.commit())return "Scoped drafts could not be saved; the previous file is retained.";
        return {};
    }
};
} // namespace pgn::social
