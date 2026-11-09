// Public scalar1 fixture; actual native BIP322, no funded wallet or network.
#include <pgn/script_work_proof.h>
#include <common/signmessage.h>
#include <key.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <streams.h>
#include <util/translation.h>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

extern const TranslateFn G_TRANSLATION_FUN=[](const char* text){return std::string(text);};
using namespace pgn::core;
static void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::vector<unsigned char> Sign(const std::string& challenge,const CKey& key){
    auto status=MessageVerificationResult::ERR_INVALID;
    const auto pair=BIP322Txs::Create(WitnessV0KeyHash(key.GetPubKey()),challenge,status);
    Need(pair.has_value(),"Native signing preparation");
    CMutableTransaction tx{pair->m_to_sign};FillableSigningProvider provider;provider.AddKey(key);
    SignatureData data;
    Need(ProduceSignature(provider,MutableTransactionSignatureCreator(tx,0,pair->m_to_spend.vout[0].nValue,SignOptions{SIGHASH_ALL}),pair->m_to_spend.vout[0].scriptPubKey,data),"Native fixture signing");
    UpdateInput(tx.vin[0],data);DataStream wire;wire<<tx.vin[0].scriptWitness.stack;
    std::vector<unsigned char> result(wire.size());std::memcpy(result.data(),wire.data(),wire.size());return result;
}
int main(){
    ECC_Context ecc;
    std::array<unsigned char,32> bytes{};bytes.back()=1;CKey key;key.Set(bytes.begin(),bytes.end(),true);
    const auto script=GetScriptForDestination(WitnessV0KeyHash(key.GetPubKey()));
    uint256 a,b,c;a.begin()[0]=1;b.begin()[0]=2;c.begin()[0]=3;
    const ScriptWorkContext context{a,b,c};const ScriptWorkBudget budget{128,4096};
    const auto challenge=ScriptWorkChallenge(context,script,budget);Need(challenge.has_value(),"Challenge");
    const auto signature=Sign(*challenge,key);
    for(unsigned i=0;i<128;++i){
        const auto checked=VerifyScriptWork(context,script,signature,budget);
        Need(checked.verdict==ScriptWorkVerdict::Verified&&checked.proof&&checked.native_result==MessageVerificationResult::OK,"Warm exact verification");
        Need(checked.proof->Context().chain==a&&checked.proof->Context().work==b&&checked.proof->Context().parent==c&&checked.proof->Script()==script,"Fresh seal binding");
    }
    const auto oracle=[&](ScriptWorkContext ctx,CScript supplied,std::vector<unsigned char> sig){
        const auto message=ScriptWorkChallenge(ctx,supplied,budget);Need(message.has_value(),"Oracle context");
        CTxDestination destination;Need(ExtractDestination(supplied,destination),"Oracle script");
        auto native_sig=sig;const auto result=MessageVerifyBIP322(destination,native_sig,*message,MessageVerificationResult::ERR_INVALID);
        const auto checked=VerifyScriptWork(ctx,supplied,sig,budget);
        Need(bool(checked.proof)==(result==MessageVerificationResult::OK),"Cached versus uncached native verdict");
        return bool(checked.proof);
    };
    for(unsigned field=0;field<3;++field){auto changed=context;
        if(field==0)changed.chain.begin()[0]^=8;if(field==1)changed.work.begin()[0]^=8;if(field==2)changed.parent.begin()[0]^=8;
        Need(!oracle(changed,script,signature),"Changed context accepted from cache");}
    auto other_script=script;other_script.back()^=1;
    Need(!oracle(context,other_script,signature),"Changed owner script accepted");
    for(unsigned i=0;i<signature.size();i+=3){auto changed=signature;changed[i]^=1;Need(!oracle(context,script,changed),"Changed signature accepted");}
    Need(VerifyScriptWork(context,script,signature,{0,4096}).verdict==ScriptWorkVerdict::InvalidBudget,"Invalid warmed budget");
    Need(VerifyScriptWork(context,script,signature,{1,4096}).verdict==ScriptWorkVerdict::BudgetExceeded,"Warmed script budget");
    Need(VerifyScriptWork(context,script,signature,{128,signature.size()-1}).verdict==ScriptWorkVerdict::BudgetExceeded,"Warmed signature budget");
    auto missing=context;missing.parent.SetNull();Need(!VerifyScriptWork(missing,script,signature,budget).proof,"Warmed null context");
    auto alternate=context;alternate.work.begin()[1]=4;
    const auto second=Sign(*ScriptWorkChallenge(alternate,script,budget),key);
    Need(oracle(alternate,script,second),"Alternate valid context");Need(!oracle(context,script,second),"Cross-context reuse");
    std::atomic<bool> valid{true};std::vector<std::thread> workers;
    for(unsigned n=0;n<8;++n)workers.emplace_back([&]{for(unsigned i=0;i<64;++i)if(!VerifyScriptWork(context,script,signature,budget).proof)valid=false;});
    for(auto& worker:workers)worker.join();Need(valid,"Concurrent cache verification");
    std::cout<<"PASS: exact native signatures, context/script/signature mutations, warmed budgets and concurrent access.\n";
}
