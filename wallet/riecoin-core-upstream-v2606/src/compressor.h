// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COMPRESSOR_H
#define BITCOIN_COMPRESSOR_H

#include <prevector.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <span.h>

#include <limits>
#include <stdexcept>
#include <type_traits>

/**
 * This saves us from making many heap allocations when serializing
 * and deserializing compressed scripts.
 *
 * This prevector size is determined by the largest .resize() in the
 * CompressScript function. The largest compressed script format is a
 * compressed public key, which is 33 bytes.
 */
using CompressedScript = prevector<33, unsigned char>;


bool CompressScript(const CScript& script, CompressedScript& out);
unsigned int GetSpecialScriptSize(unsigned int nSize);
bool DecompressScript(CScript& script, unsigned int nSize, const CompressedScript& in);

/**
 * Compress amount.
 *
 * nAmount is of type uint64_t and thus cannot be negative. If you're passing in
 * a CAmount (int64_t), make sure to properly handle the case where the amount
 * is negative before calling CompressAmount(...).
 *
 * The inherited valid Riecoin codebook is preserved. Wider PGN atomic domains
 * require an explicit storage qualification: non-encodable expansion throws
 * std::overflow_error BEFORE producing any wrapped alias. This is not a chain
 * ceiling or approval of all CAmount values with the legacy storage schema.
 */
uint64_t CompressAmount(uint64_t nAmount);

uint64_t DecompressAmount(uint64_t nAmount);

/** Compact serializer for scripts.
 *
 *  It detects common cases and encodes them much more efficiently.
 *  3 special cases are defined:
 *  * Pay to pubkey hash (encoded as 21 bytes)
 *  * Pay to script hash (encoded as 21 bytes)
 *  * Pay to pubkey starting with 0x02, 0x03 or 0x04 (encoded as 33 bytes)
 *
 *  Other scripts up to 121 bytes require 1 byte + script length. Above
 *  that, scripts up to 16505 bytes require 2 bytes + script length.
 */
struct ScriptCompression
{
    /**
     * make this static for now (there are only 6 special scripts defined)
     * this can potentially be extended together with a new version for
     * transactions, in which case this value becomes dependent on version
     * and nHeight of the enclosing transaction.
     */
    static const unsigned int nSpecialScripts = 6;

    template<typename Stream>
    void Ser(Stream &s, const CScript& script) {
        CompressedScript compr;
        if (CompressScript(script, compr)) {
            s << std::span{compr};
            return;
        }
        unsigned int nSize = script.size() + nSpecialScripts;
        s << VARINT(nSize);
        s << std::span{script};
    }

    template<typename Stream>
    void Unser(Stream &s, CScript& script) {
        unsigned int nSize = 0;
        s >> VARINT(nSize);
        if (nSize < nSpecialScripts) {
            CompressedScript vch(GetSpecialScriptSize(nSize), 0x00);
            s >> std::span{vch};
            DecompressScript(script, nSize, vch);
            return;
        }
        nSize -= nSpecialScripts;
        if (nSize > MAX_SCRIPT_SIZE) {
            // Overly long script, replace with a short invalid one
            script << OP_RETURN;
            s.ignore(nSize);
        } else {
            script.resize(nSize);
            s >> std::span{script};
        }
    }
};

struct AmountCompression
{
    template<typename Stream, typename I> void Ser(Stream& s, I val)
    {
        static_assert(std::is_integral_v<I> && !std::is_same_v<I, bool> && sizeof(I) <= sizeof(uint64_t));
        if constexpr (std::is_signed_v<I>) {
            if (val < 0) throw std::invalid_argument("Negative amount cannot be compressed");
        }
        const auto encoded = CompressAmount(static_cast<uint64_t>(val));
        s << VARINT(encoded);
    }
    template<typename Stream, typename I> void Unser(Stream& s, I& val)
    {
        static_assert(std::is_integral_v<I> && !std::is_same_v<I, bool> && sizeof(I) <= sizeof(uint64_t));
        uint64_t v;
        s >> VARINT(v);
        const uint64_t decoded = DecompressAmount(v);
        if (decoded > static_cast<uint64_t>(std::numeric_limits<I>::max()))
            throw std::overflow_error("Decompressed amount does not fit its target type");
        val = static_cast<I>(decoded); // Commit only after exact range validation.
    }
};

/** wrapper for CTxOut that provides a more compact serialization */
struct TxOutCompression
{
    FORMATTER_METHODS(CTxOut, obj) { READWRITE(Using<AmountCompression>(obj.nValue), Using<ScriptCompression>(obj.scriptPubKey)); }
};

#endif // BITCOIN_COMPRESSOR_H
