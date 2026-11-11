#pragma once
#include "social/social_data.h"
#include <QListWidget>
#include <functional>
namespace pgn::ui {
class ForumThread final:public QListWidget {
public:
    explicit ForumThread(QWidget* parent=nullptr);
    void setTopic(const social::Entry& topic,bool preserve=false);
    QString selectedPost()const;
    std::function<void(QString,QString)> onQuote;
    std::function<void(QString)> onReply;
    std::function<void(QString,QPoint)> onMenu;
    std::function<void(QString,QString)> onReact;
    std::function<QString(QString,QString)> reactionUnavailable;
    bool canDraft{};
protected:void resizeEvent(QResizeEvent* event)override;
private:void reflow();
};
}
