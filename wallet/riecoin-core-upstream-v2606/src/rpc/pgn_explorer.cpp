// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see COPYING.
#include <chain.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <index/txindex.h>
#include <key_io.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <node/transaction.h>
#include <primitives/block.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/solver.h>
#include <util/strencodings.h>
#include <validation.h>
#include <univalue.h>
#include <algorithm>
#include <span>

namespace {
constexpr size_t PREVIEW_BYTES{96};
constexpr int MAX_PAGE{25};

UniValue Summary(const CTransaction& tx)
{
    UniValue out{UniValue::VOBJ};
    out.pushKV("txid", tx.GetHash().GetHex());
    out.pushKV("hash", tx.GetWitnessHash().GetHex());
    out.pushKV("size", ::GetSerializeSize(TX_WITH_WITNESS(tx)));
    out.pushKV("vsize", (GetTransactionWeight(tx) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR);
    out.pushKV("weight", GetTransactionWeight(tx));
    out.pushKV("version", tx.version);
    out.pushKV("locktime", uint64_t{tx.nLockTime});
    // An atomic decimal string is independent of JSON floating-point parsing.
    out.pushKV("output_atomic", std::to_string(tx.GetValueOut()));
    out.pushKV("vin_total", uint64_t{tx.vin.size()});
    out.pushKV("vout_total", uint64_t{tx.vout.size()});
    return out;
}

UniValue TransactionPage(const CTransaction& tx, int offset, int limit)
{
    UniValue out{Summary(tx)};
    out.pushKV("io_offset", offset);
    out.pushKV("io_limit", limit);
    UniValue inputs{UniValue::VARR}, outputs{UniValue::VARR};
    for (size_t i = std::min(size_t(offset), tx.vin.size()); i < std::min(size_t(offset + limit), tx.vin.size()); ++i) {
        const auto& in = tx.vin[i];
        UniValue item{UniValue::VOBJ};
        item.pushKV("n", uint64_t{i});
        item.pushKV("sequence", uint64_t{in.nSequence});
        if (in.prevout.IsNull()) {
            item.pushKV("coinbase", HexStr(std::span{in.scriptSig.data(), std::min<size_t>(PREVIEW_BYTES, in.scriptSig.size())}));
        } else {
            item.pushKV("txid", in.prevout.hash.GetHex());
            item.pushKV("vout", uint64_t{in.prevout.n});
        }
        item.pushKV("script_sig_bytes", uint64_t{in.scriptSig.size()});
        inputs.push_back(std::move(item));
    }
    for (size_t i = std::min(size_t(offset), tx.vout.size()); i < std::min(size_t(offset + limit), tx.vout.size()); ++i) {
        const auto& value = tx.vout[i];
        UniValue item{UniValue::VOBJ}, script{UniValue::VOBJ};
        item.pushKV("n", uint64_t{i});
        item.pushKV("value", ValueFromAmount(value.nValue));
        script.pushKV("bytes", uint64_t{value.scriptPubKey.size()});
        script.pushKV("hex_preview", HexStr(std::span{value.scriptPubKey.data(), std::min<size_t>(PREVIEW_BYTES, value.scriptPubKey.size())}));
        script.pushKV("truncated", value.scriptPubKey.size() > PREVIEW_BYTES);
        CTxDestination destination;
        if (ExtractDestination(value.scriptPubKey, destination)) script.pushKV("address", EncodeDestination(destination));
        else if (!value.scriptPubKey.empty() && value.scriptPubKey.front() == OP_RETURN) script.pushKV("type", "nulldata");
        item.pushKV("scriptPubKey", std::move(script));
        outputs.push_back(std::move(item));
    }
    out.pushKV("vin", std::move(inputs));
    out.pushKV("vout", std::move(outputs));
    return out;
}

static RPCMethod getpgnexplorerview()
{
    return RPCMethod{
        "getpgnexplorerview",
        "Read-only bounded block or transaction projection. Scripts are explicit96-byte previews; exact atomic totals cover the whole transaction. No index or wallet is activated.\n",
        {
            {"kind", RPCArg::Type::STR, RPCArg::Optional::NO, "block or transaction"},
            {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Block hash or transaction ID"},
            {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Default{""}, "Optional transaction block; avoids requiring txindex"},
            {"offset", RPCArg::Type::NUM, RPCArg::Default{0}, "Zero-based transaction or input/output page offset"},
            {"limit", RPCArg::Type::NUM, RPCArg::Default{25}, "Maximum rows per array,1..25"},
        },
        RPCResult{RPCResult::Type::OBJ, "", "Bounded explorer projection", {{RPCResult::Type::ELISION, "", "Header, exact totals and explicitly paginated rows"}}},
        RPCExamples{HelpExampleCli("getpgnexplorerview", "block hash")},
        [](const RPCMethod&, const JSONRPCRequest& request) -> UniValue {
            const auto kind = request.params[0].get_str();
            if (kind != "block" && kind != "transaction") throw JSONRPCError(RPC_INVALID_PARAMETER, "kind must be block or transaction");
            const auto id = ParseHashV(request.params[1], "id");
            const auto anchor = request.params[2].isNull() ? std::string{} : request.params[2].get_str();
            const int offset = request.params[3].isNull() ? 0 : request.params[3].getInt<int>();
            const int limit = request.params[4].isNull() ? MAX_PAGE : request.params[4].getInt<int>();
            if (offset < 0 || offset > 1000000 || limit < 1 || limit > MAX_PAGE) throw JSONRPCError(RPC_INVALID_PARAMETER, "offset must be0..1000000 and limit1..25");
            const auto& node = EnsureAnyNodeContext(request.context);
            auto& chainman = EnsureChainman(node);
            const CBlockIndex* index{}; const CBlockIndex* tip{};
            UniValue out;
            if (kind == "block") {
                if (!anchor.empty()) throw JSONRPCError(RPC_INVALID_PARAMETER, "block lookup does not accept a transaction block anchor");
                {
                    LOCK(cs_main);
                    index = chainman.m_blockman.LookupBlockIndex(id);
                    tip = chainman.ActiveChain().Tip();
                }
                if (!index) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
                CBlock block;
                if (!chainman.m_blockman.ReadBlock(block, *index)) throw JSONRPCError(RPC_MISC_ERROR, "Block data unavailable");
                out = blockheaderToJSON(*tip, *index, chainman.GetConsensus().nBitsMin);
                out.pushKV("size", ::GetSerializeSize(TX_WITH_WITNESS(block)));
                out.pushKV("weight", GetBlockWeight(block));
                out.pushKV("coinbase_output_atomic", block.vtx.empty() ? "0" : std::to_string(block.vtx.front()->GetValueOut()));
                out.pushKV("tx_total", uint64_t{block.vtx.size()});out.pushKV("tx_offset", offset);out.pushKV("tx_limit", limit);
                UniValue txs{UniValue::VARR};
                for (size_t i = std::min(size_t(offset), block.vtx.size()); i < std::min(size_t(offset + limit), block.vtx.size()); ++i) txs.push_back(Summary(*block.vtx[i]));
                out.pushKV("tx", std::move(txs));
            } else {
                if (!anchor.empty()) {
                    const auto hash = ParseHashV(request.params[2], "blockhash");
                    LOCK(cs_main);index = chainman.m_blockman.LookupBlockIndex(hash);
                    if (!index) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
                } else if (g_txindex) g_txindex->BlockUntilSyncedToCurrentChain();
                uint256 hashBlock;
                const auto tx = node::GetTransaction(index, node.mempool.get(), Txid::FromUint256(id), chainman.m_blockman, hashBlock);
                if (!tx) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, !anchor.empty() ? "No such transaction in the provided block" : !g_txindex ? "Transaction unavailable. Use -txindex or provide its block hash" : "No such indexed or mempool transaction");
                out = TransactionPage(*tx, offset, limit);
                if (!hashBlock.IsNull()) {
                    LOCK(cs_main);index = chainman.m_blockman.LookupBlockIndex(hashBlock);
                    out.pushKV("blockhash", hashBlock.GetHex());
                    const bool active = index && chainman.ActiveChain().Contains(*index);
                    out.pushKV("in_active_chain", active);
                    out.pushKV("confirmations", active ? 1 + chainman.ActiveChain().Height() - index->nHeight : -1);
                    if (index) out.pushKV("blocktime", index->GetBlockTime());
                }
            }
            out.pushKV("schema", "pgn-explorer-view-1");out.pushKV("kind", kind);
            if (out.write().size() > 65536) throw JSONRPCError(RPC_INTERNAL_ERROR, "Explorer projection exceeds its64KiB contract");
            return out;
        },
    };
}
}

void RegisterPgnExplorerRPCCommands(CRPCTable& table)
{
    static const CRPCCommand command{"blockchain", &getpgnexplorerview};
    table.appendCommand(command.name, &command);
}
