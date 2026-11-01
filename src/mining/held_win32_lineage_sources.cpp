#include "mining/held_win32_lineage_sources.h"
#include <stdexcept>
#include <utility>

namespace pgn::mining {
namespace {
class Reader final:public HeldSourceLineageReader {
public:
    Reader(QString run,std::unique_ptr<SourceMirrorFrozenFile> file):run_(std::move(run)),file_(std::move(file)){}
    QString run()const override{return run_;}
    QString source()const override{return "native/"+run_+".stdout";}
    bool isRegularFile()const override{return bool(file_);}
    bool isSymbolicLink()const override{return false;} // handle constructor rejected reparse/nonregular
    qint64 size()const override{requireOpen();return file_->size();}
    QByteArray read(qint64 offset,qsizetype maximum)override{requireOpen();return file_->read(offset,maximum);}
    void closeAndVerifyStable()override{requireOpen();auto file=std::move(file_);file->closeAndVerifyStable();}
private:
    void requireOpen()const{if(!file_)throw std::runtime_error("Held lineage source already closed");}
    QString run_;
    std::unique_ptr<SourceMirrorFrozenFile> file_;
};
}
HeldWin32LineageSources::HeldWin32LineageSources(SourceMirror& mirror):freeze_(mirror.freezeForLineage()){}
OpenHeldLineageSource HeldWin32LineageSources::opener()const{
    if(!freeze_)throw std::runtime_error("Held lineage scope moved/released");
    return [freeze=freeze_](const QString& run,const QString& source)->std::unique_ptr<HeldSourceLineageReader>{
        // Mirror validates the canonical run BEFORE file access; reject wrong
        // source labels here instead of normalizing/adopting another path.
        if(source!="native/"+run+".stdout")throw std::runtime_error("Held lineage source label mismatch");
        return std::make_unique<Reader>(run,freeze->open(run));
    };
}
}
