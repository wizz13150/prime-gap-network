#include "mining/committed_work.h"
#include <atomic>
#include <thread>
#include <iostream>
int main(){
 using namespace pgn::mining;CommittedWorkChannel channel;
 if(channel.observe()||channel.publish({})!=CommitResult::invalid)return 1;
 const CommittedWork zero{1000,0,0,0,0,0};
 if(channel.publish(zero)!=CommitResult::published||channel.observe()->positions!=0)return 2;
 if(channel.publish({2000,0,0,0,0,0})!=CommitResult::duplicate||channel.observe()->sourceTimeNs!=1000)return 3;
 std::atomic<bool> done=false;std::atomic<int> failure=0;
 std::thread writer([&]{for(uint64_t i=1;i<=100000;++i)
  if(channel.publish({1000+i,i,i*3,i,i*2,i*4})!=CommitResult::published){failure=4;break;}
  done=true;});
 while(!done.load()){const auto read=channel.observe();if(!read||read->tests!=read->positions*3||
  read->batches!=read->positions||read->gaps!=read->positions*2||read->processedGaps!=read->positions*4||
  read->sourceTimeNs!=1000+read->positions){failure=5;break;}}
 writer.join();if(failure)return failure.load();const auto final=*channel.observe();
 if(final.positions!=100000||channel.publish({final.sourceTimeNs,100001,300003,100001,200002,400004})!=CommitResult::invalid)return 6;
 if(channel.publish({final.sourceTimeNs+1,99999,300000,100000,200000,400000})!=CommitResult::invalid||
  channel.observe()->sourceTimeNs!=final.sourceTimeNs)return 7;
 std::cout<<"PASS100000 coherent whole-vector commits;originalzero/age;duplicate/regression rejected;no periodic rawtrace replacement\n";
}
