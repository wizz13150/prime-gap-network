#pragma once
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QUrl>
#include <QVector>
namespace pgn::social::bbcode {
// One stored/source representation. No HTML parser, web view, resource fetch or
// HTML -> BBCode round trip. Unknown/malformed input remains literal.
constexpr int MaxSource=4000,MaxDepth=16,MaxTokens=1024;
class Document final:public QTextDocument {
public:using QTextDocument::QTextDocument;
protected:QVariant loadResource(int,const QUrl&)override{return {};}
};
struct Node{QString tag,text,target;QVector<Node> children;};
inline bool supported(const QString& tag){return QStringList{"b","i","u","s","quote","code","url"}.contains(tag);}
inline bool webUrl(const QString& text){
    const QUrl url(text,QUrl::StrictMode);return text.size()<=2048&&url.isValid()&&!url.host().isEmpty()
        &&(url.scheme()=="https"||url.scheme()=="http")&&url.userInfo().isEmpty();
}
class Parser{
    const QString& source_;int pos_{},tokens_{};bool valid_{true};
    QVector<Node> part(const QString& close,int depth){
        QVector<Node> result;
        while(pos_<source_.size()){
            if(source_[pos_]!='['){int end=source_.indexOf('[',pos_);if(end<0)end=source_.size();result.append({{},source_.mid(pos_,end-pos_),{}, {}});pos_=end;continue;}
            const int start=pos_,end=source_.indexOf(']',start);if(end<0){result.append({{},source_.mid(pos_),{}, {}});pos_=source_.size();break;}
            if(++tokens_>MaxTokens){valid_=false;break;}
            const QString token=source_.mid(start+1,end-start-1),lower=token.toLower();pos_=end+1;
            if(lower=="/"+close&&!close.isEmpty())return result;
            if(lower.startsWith('/')&&supported(lower.mid(1))){valid_=false;break;}
            QString tag=lower,target;if(lower.startsWith("url=")){tag="url";target=token.mid(4);}
            if(!supported(tag)){result.append({{},source_.mid(start,end-start+1),{}, {}});continue;}
            if(depth>=MaxDepth){valid_=false;break;}
            Node node;node.tag=tag;node.target=target;
            if(tag=="code"){
                const int finish=source_.indexOf("[/code]",pos_,Qt::CaseInsensitive);if(finish<0){valid_=false;break;}
                node.text=source_.mid(pos_,finish-pos_);pos_=finish+7;
            }else{
                const int body=pos_;node.children=part(tag,depth+1);
                if(tag=="url"&&target.isEmpty())node.target=source_.mid(body,pos_-body-6);
                if(tag=="url"&&!webUrl(node.target)){valid_=false;break;}
            }
            result.append(node);if(!valid_)break;
        }
        if(!close.isEmpty())valid_=false;return result;
    }
public:
    explicit Parser(const QString& source):source_(source){}
    QVector<Node> parse(){return part({},0);}
    bool valid()const{return valid_;}
};
inline void write(QTextCursor& cursor,const QVector<Node>& nodes,QTextCharFormat format){
    for(const auto& node:nodes){
        if(node.tag.isEmpty()){cursor.insertText(node.text,format);continue;}
        auto changed=format;
        if(node.tag=="b")changed.setFontWeight(QFont::Bold);
        if(node.tag=="i")changed.setFontItalic(true);
        if(node.tag=="u")changed.setFontUnderline(true);
        if(node.tag=="s")changed.setFontStrikeOut(true);
        if(node.tag=="code"){
            changed.setFontFamilies({"Consolas","monospace"});changed.setBackground(QColor("#f4f4f4"));
            cursor.insertText(node.text,changed);continue;
        }
        if(node.tag=="quote"){
            const auto previous=cursor.blockFormat();if(cursor.positionInBlock())cursor.insertBlock();
            auto block=previous;block.setLeftMargin(previous.leftMargin()+20);block.setTopMargin(5);block.setBottomMargin(5);cursor.setBlockFormat(block);
            write(cursor,node.children,changed);cursor.insertBlock(previous,format);continue;
        }
        if(node.tag=="url"){changed.setForeground(QColor("#0f62fe"));changed.setFontUnderline(true);}
        write(cursor,node.children,changed);
        if(node.tag=="url"){
            QString label;for(const auto& child:node.children)label+=child.text;
            if(label!=node.target)cursor.insertText(" ("+node.target+")",format);
        }
    }
}
inline bool render(QTextDocument& document,const QString& source,int sourceLimit=MaxSource){
    document.clear();QTextCursor cursor(&document);QTextCharFormat format;format.setFont(document.defaultFont());format.setForeground(QColor("#161616"));
    sourceLimit=qBound(1,sourceLimit,16000);if(source.size()>sourceLimit){cursor.insertText(source.left(sourceLimit)+"\n[BBCode display limit reached]",format);return false;}
    Parser parser(source);const auto nodes=parser.parse();
    if(!parser.valid()){cursor.insertText(source,format);return false;}
    write(cursor,nodes,format);return true;
}
inline QString opening(const QString& tag){return tag=="url"?"[url=https://example.org]":"["+tag+"]";}
} // namespace pgn::social::bbcode
