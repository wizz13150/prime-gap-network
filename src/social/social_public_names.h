#pragma once
#include "social/social_data.h"
#include <QMap>

namespace pgn::social {
// Public labels are display data, never credentials or permissions. A complete
// same-anchor Total projection is required before any script is resolved.
class PublicNames {
    struct Name {QString operatorId,display;bool operator==(const Name&)const=default;};
    Identity requested_,publishedIdentity_;
    std::optional<LeaderboardPeriod> anchor_;
    QDateTime observed_,publishedUtc_;
    QSet<QString> operators_,ambiguous_,cursors_;
    QMap<QString,QString> scripts_;
    QMap<QString,Name> candidates_,published_;
    QString next_;
    qsizetype bytes_{};int rows_{};quint64 revision_{};bool collecting_{};
    QString fail(QString reason){clear();return reason;}
public:
    static constexpr int MaximumRows=500,MaximumBytes=1024*1024;
    void clear(){if(!published_.isEmpty())++revision_;published_.clear();publishedUtc_={};collecting_=false;}
    void begin(const Identity& identity){
        if(identity!=publishedIdentity_)clear();requested_=identity;anchor_.reset();observed_={};
        operators_.clear();ambiguous_.clear();cursors_.clear();scripts_.clear();candidates_.clear();next_.clear();bytes_=rows_=0;collecting_=true;
    }
    QString append(const ReadRequest& request,const Snapshot& page){
        if(!collecting_||request.identity!=requested_||request.section!=Section::Miners||request.window!=Window::AllTime||request.cursor!=next_)return fail("Public-name lookup scope changed.");
        const auto error=validate(page,request);if(!error.isEmpty())return fail(error);
        if(!page.leaderboardPeriod||!validLeaderboardPeriod(*page.leaderboardPeriod,Window::AllTime)||page.leaderboardPeriod->total<0)return fail("Public-name anchor or exhaustive row count is unavailable.");
        if(!anchor_){anchor_=page.leaderboardPeriod;observed_=page.observedUtc;}
        else if(anchor_!=page.leaderboardPeriod)return fail("Public-name pages changed canonical anchor.");
        if(rows_+page.miners.size()>MaximumRows)return fail("Public-name projection exceeds the bounded display lookup.");
        for(const auto& miner:page.miners){
            if(operators_.contains(miner.id)||miner.id==QString(64,'0')||!QRegularExpression("^[0-9a-f]{64}$").match(miner.id).hasMatch())return fail("Repeated or invalid public-name operator.");
            operators_.insert(miner.id);++rows_;
            if(!miner.publicIdentity||!miner.publicIdentity->aliasesComplete||miner.publicIdentity->signingScripts.isEmpty())return fail("Complete public signing aliases are unavailable.");
            const auto& identity=*miner.publicIdentity;
            bytes_+=miner.id.size()*2+miner.name.size()*2+identity.displayName.size()*2+256;
            for(const auto& script:identity.signingScripts){
                bytes_+=script.size()*2+128;if(bytes_>MaximumBytes)return fail("Public-name lookup byte limit reached.");
                if(scripts_.contains(script)&&scripts_[script]!=miner.id)ambiguous_.insert(script);
                else scripts_.insert(script,miner.id);
            }
            if(!identity.displayName.isEmpty())candidates_.insert(miner.id,{miner.id,publicIdentityDisplayName(miner.id,identity.displayName)});
        }
        if(!page.nextCursor.isEmpty()){
            if(cursors_.contains(page.nextCursor)||page.nextCursor==request.cursor)return fail("Repeated public-name page.");
            cursors_.insert(page.nextCursor);next_=page.nextCursor;return {};
        }
        if(rows_!=anchor_->total)return fail("Public-name projection is incomplete.");
        QMap<QString,Name> completed;
        for(auto it=scripts_.cbegin();it!=scripts_.cend();++it)if(!ambiguous_.contains(it.key())&&candidates_.contains(it.value()))completed.insert(it.key(),candidates_[it.value()]);
        if(completed!=published_||publishedIdentity_!=requested_)++revision_;
        published_=std::move(completed);publishedIdentity_=requested_;publishedUtc_=observed_;collecting_=false;next_.clear();return {};
    }
    QString nextCursor()const{return next_;}
    bool collecting()const{return collecting_;}
    int remaining()const{return MaximumRows-rows_;}
    quint64 revision()const{return revision_;}
    QString display(const Identity& context,const QString& script,QDateTime now=QDateTime::currentDateTimeUtc())const{
        if(context!=publishedIdentity_||!current(publishedUtc_,now))return {};
        return published_.value(script).display;
    }
};
}
