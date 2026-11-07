#include "ui/vanity_import_count.h"
#include "ui/vanity_batch.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
static void need(bool good){if(!good)throw std::runtime_error("Vanity import count contract");}
int main(int argc,char**argv){
    QCoreApplication application(argc,argv);QSet<QString> addresses;int successes=0;
    // Opaque stand-ins for already inspected canonical public identities.
    successes+=pgn::ui::rememberVanityImportedAddress(addresses,"verified-A");
    successes+=pgn::ui::rememberVanityImportedAddress(addresses,"verified-A");
    need(successes==1&&addresses.size()==1);
    successes+=pgn::ui::rememberVanityImportedAddress(addresses,"verified-B");
    need(successes==2&&!pgn::ui::rememberVanityImportedAddress(addresses,QString{}));
    auto nextChunk=std::move(addresses);
    successes+=pgn::ui::rememberVanityImportedAddress(nextChunk,"verified-B");
    successes+=pgn::ui::rememberVanityImportedAddress(nextChunk,"verified-C");
    need(successes==3&&nextChunk.size()==3);
    QSet<QString> nextOperation;need(pgn::ui::rememberVanityImportedAddress(nextOperation,"verified-A"));
    // Exact identities are not case-folded or otherwise reinterpreted here.
    need(pgn::ui::rememberVanityImportedAddress(nextOperation,"verified-a"));
    for(int target:{1,5,10,17,1000}){pgn::ui::VanityBatch batch;need(batch.start(target));for(int i=0;i<target;++i){auto token=batch.beginItem();need(token);need(batch.accept(token)==(i+1==target?pgn::ui::VanityBatch::Completion::Finish:pgn::ui::VanityBatch::Completion::Next));}need(batch.found()==unsigned(target)&&batch.finish());}
    pgn::ui::VanityBatch continuous;need(continuous.start(0));auto token=continuous.beginItem();need(continuous.accept(token)==pgn::ui::VanityBatch::Completion::Next);continuous.stop();need(continuous.finish());
    std::cout<<"PASS distinct successful address count across duplicates/chunks/new operations; existing finite/continuous batch model, no key generation\n";
}
