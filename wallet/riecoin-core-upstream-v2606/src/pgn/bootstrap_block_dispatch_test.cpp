// MIT. Native dispatch boundary; no daemon, wallet or network.
#include <pgn/bootstrap_block_dispatch.h>
#include <util/translation.h>
#include <iostream>
#include <stdexcept>

extern const TranslateFn G_TRANSLATION_FUN = [](const char* text) { return std::string(text); };
namespace {
void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
}

int main()
{
    using namespace pgn::core;
    uint256 identity;
    identity.begin()[0] = 1;
    CBlockIndex parent;
    parent.phashBlock = &identity;
    NativeBlockScienceBudget budget{};
    budget.maximum_coinbase_outputs = 4;
    budget.maximum_script_bytes = 256;
    budget.maximum_payload_bytes = 1024;
    budget.signature.signature_bytes = 4096;

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript{} << OP_1;
    tx.vout = {CTxOut{0, CScript{} << OP_RETURN}};
    const auto make = [&](const CMutableTransaction& transaction) {
        CBlock block;
        block.hashPrevBlock = identity;
        block.vtx = {MakeTransactionRef(transaction)};
        block.hashMerkleRoot = BlockMerkleRoot(block);
        return block;
    };
    const auto body = make(tx);
    Require(CanDispatchDirectlyToNativeBootstrap(body, parent, budget), "Absent-science dispatch");
    tx.vout[0].nValue = 1;
    Require(CanDispatchDirectlyToNativeBootstrap(make(tx), parent, budget), "Dispatch must not pretend to authorize a payment");
    auto bad = body;
    bad.hashMerkleRoot.SetNull();
    Require(!CanDispatchDirectlyToNativeBootstrap(bad, parent, budget), "Merkle binding");
    bad = body;
    bad.hashPrevBlock.SetNull();
    Require(!CanDispatchDirectlyToNativeBootstrap(bad, parent, budget), "Parent binding");
    CBlockIndex missing_parent;
    Require(!CanDispatchDirectlyToNativeBootstrap(body, missing_parent, budget), "Missing parent identity");
    bad = body;
    bad.vtx.clear();
    Require(!CanDispatchDirectlyToNativeBootstrap(bad, parent, budget), "Missing coinbase");
    bad.vtx.push_back(CTransactionRef{});
    Require(!CanDispatchDirectlyToNativeBootstrap(bad, parent, budget), "Null coinbase");
    bad = body;
    bad.vtx.push_back(body.vtx.front());
    bad.hashMerkleRoot = BlockMerkleRoot(bad);
    Require(!CanDispatchDirectlyToNativeBootstrap(bad, parent, budget), "Additional transaction");
    auto non_coinbase = tx;
    non_coinbase.vin[0].prevout.n = 0; // A null hash with a non-null index is not a coinbase input.
    Require(!CanDispatchDirectlyToNativeBootstrap(make(non_coinbase), parent, budget), "Not a coinbase");

    for (unsigned field = 0; field < 4; ++field) {
        auto invalid = budget;
        if (field == 0) invalid.maximum_coinbase_outputs = 0;
        if (field == 1) invalid.maximum_script_bytes = 0;
        if (field == 2) invalid.maximum_payload_bytes = 0;
        if (field == 3) invalid.signature.signature_bytes = 0;
        Require(!CanDispatchDirectlyToNativeBootstrap(body, parent, invalid), "Invalid budget");
    }
    tx.vout.assign(5, CTxOut{0, CScript{} << OP_RETURN});
    Require(!CanDispatchDirectlyToNativeBootstrap(make(tx), parent, budget), "Output bound");
    CScript oversized;
    for (unsigned i = 0; i < 257; ++i) oversized.push_back(OP_0);
    tx.vout = {CTxOut{0, oversized}};
    Require(!CanDispatchDirectlyToNativeBootstrap(make(tx), parent, budget), "Script bound");

    const Bytes tag{BlockScienceEnvelopeTag.begin(), BlockScienceEnvelopeTag.end()};
    CScript nonminimal;
    nonminimal << OP_RETURN;
    nonminimal.push_back(OP_PUSHDATA1);
    nonminimal.push_back(tag.size());
    nonminimal.insert(nonminimal.end(), tag.begin(), tag.end());
    nonminimal << OP_0;
    for (const auto& script : {CScript{} << OP_RETURN << tag,
                              CScript{} << OP_RETURN << tag << Bytes{},
                              CScript{} << OP_RETURN << tag << Bytes{1, 2, 3},
                              nonminimal}) {
        tx.vout = {CTxOut{0, script}};
        Require(!CanDispatchDirectlyToNativeBootstrap(make(tx), parent, budget), "Known science tag retains full verification");
    }
    std::cout << "Native bootstrap routing boundaries passed; dispatch grants no acceptance.\n";
}
