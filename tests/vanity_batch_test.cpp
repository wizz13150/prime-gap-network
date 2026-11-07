#include "ui/vanity_batch.h"
#include <iostream>
#include <stdexcept>
using pgn::ui::VanityBatch;
static void require(bool okay,const char* error){if(!okay)throw std::runtime_error(error);}
int main(){
    VanityBatch batch;
    require(!batch.start(-1)&&!batch.start(1001),"Out-of-range address counts accepted");
    require(batch.start(5)&&!batch.start(10),"An active batch was overwritten");
    std::uint64_t last{};
    for(int i=0;i<5;++i){last=batch.beginItem();require(last&&batch.beginItem()==0,"Concurrent item admitted");
        require(batch.accept(last)==(i==4?VanityBatch::Completion::Finish:VanityBatch::Completion::Next),"Wrong finite batch limit");
        require(batch.accept(last)==VanityBatch::Completion::Ignored,"Duplicate verification counted twice");}
    require(batch.found()==5&&batch.beginItem()==0&&batch.finish(),"Finite batch did not finish before restoring controls");
    require(batch.start(0),"Continuous mode rejected");auto token=batch.beginItem();
    require(token!=last&&batch.accept(last)==VanityBatch::Completion::Ignored,"Old batch completion affected a new run");
    require(batch.accept(token)==VanityBatch::Completion::Next,"Continuous mode stopped after one result");
    token=batch.beginItem();batch.stop();require(batch.accept(token)==VanityBatch::Completion::Finish&&batch.found()==2,"Cancellation lost an already verified result or started another item");
    require(batch.finish()&&batch.start(1000),"Largest finite batch rejected");token=batch.beginItem();
    require(batch.fail(token)&&batch.found()==0&&batch.beginItem()==0&&batch.finish(),"Verification failure was counted or retried silently");
    require(batch.start(1),"Single-address mode rejected");token=batch.beginItem();batch.stop();require(batch.fail(token)&&batch.finish(),"Cancel during preparation did not close safely");
    std::cout<<"PASS vanity batch counts1..1000/continuous, stop/verification races, duplicate callbacks and stale-generation isolation\n";
}
