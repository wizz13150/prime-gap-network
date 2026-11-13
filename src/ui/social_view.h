#pragma once
#include "social/social_data.h"
#include "social/social_drafts.h"
#include <QWidget>
#include <QTimer>
#include <QMap>
#include <QPointer>
#include <array>
class QLabel;class QLineEdit;class QPlainTextEdit;class QPushButton;class QTabWidget;
class QListWidget;class QTextBrowser;class QTableWidget;class QComboBox;class QStackedWidget;
class QDialog;
namespace pgn::ui {
class ForumThread;
class SocialView final:public QWidget {
public:
    explicit SocialView(QString draftPath,social::Services services={},QWidget* parent=nullptr);
    ~SocialView()override;
    void setIdentity(social::Identity identity);
    void refresh();void selectSection(social::Section section);bool saveDrafts();
private:
    struct Page{
        QLabel *counter{},*saved{},*reason{},*empty{},*count{},*composeContext{},*windowInfo{};
        QLineEdit *title{},*reference{},*search{};
        QComboBox *filter{},*category{};
        QWidget *titleRow{},*categoryRow{};
        QPlainTextEdit* draft{};
        QPushButton *refresh{},*more{},*send{},*save{},*reply{};
        QListWidget* list{};QTextBrowser *detail{},*preview{};QTableWidget* ranking{};
        ForumThread* thread{};QLabel *threadTitle{},*threadState{};QPushButton* moreReplies{};
        QStackedWidget *journey{},*dataSlot{};
        QVector<social::Entry> entries;QVector<social::Miner> miners;
        QDateTime observedUtc;QString selectedId,nextCursor,state{"offline"},statusText;
        bool loaded{},busy{},replyMode{};quint64 generation{};
        std::optional<social::LeaderboardPeriod> leaderboardPeriod;
        bool threadBusy{};quint64 threadGeneration{};
        bool canSend{},sending{};QString sendWhy,sendNotice;
    };
    social::Services services_;social::Identity identity_;social::DraftStore store_;social::Drafts drafts_;
    quint64 sendEpoch_{};
    QMap<QString,social::Drafts> pending_;
    std::array<Page,4> pages_;QTabWidget* tabs_{};QLabel *account_{},*network_{},*status_{};QTimer autosave_;
    bool dirty_{},loadFailed_{},loadingDraft_{},chatPreedit_{};QString loadError_,replyId_,replyPostId_;
    int sortColumn_{1};bool sortDescending_{true};
    QListWidget *channels_{},*members_{},*forumCategories_{};QWidget* membersPane_{};QLabel *channelTitle_{},*channelDescription_{},*memberCount_{};
    QTextBrowser* chatPreview_{};QStackedWidget* chatEditor_{};QPushButton* previewChat_{};
    QString channel_;QVector<social::Channel> channelData_;std::optional<social::AccessSnapshot> access_;
    QPointer<QDialog> accessDialog_;
    QWidget* makeComposer(int index);
    void chatDirectory(const social::Snapshot& snapshot);void selectChannel();void previewChat();void formatChat(QString tag);
    void formatDraft(int index,QString tag);void replyToPost(QString post,QString quote={});void readThread();void reactToPost(QString post,QString reaction);social::AccessRequest postRequest(QString post,social::AccessAction action,QString reaction={})const;
    void openAccess(QString member={},QString channel={});void contextMenu(int index,const QPoint& pos,bool reading=false,QString post={});void manageContext(social::AccessRequest request);
    void draftChanged(int index);void loadEditor(int index);
    void updateChatSend();void sendChat();
    void openRead(int index);void openComposer(int index,bool reply=false);void back(int index);void previewPublication(int index=1);
    void showDetail(int index,bool preserveScroll=false);void filter(int index);void renderMiners();void renderList(int index);
    void read(int index,bool more=false);void present(int index,const social::Snapshot& snapshot,bool append);
    void updateAccess();void updateStatus();void pageState(int index,QString state,QString message,QString detail={});
    bool eventFilter(QObject* object,QEvent* event)override;
};
}
