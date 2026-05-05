// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <pos_kernel.h>
#include <script/standard.h>
#include <crypto/common.h>
#include <hash.h>
#include <streams.h>
#include <tinyformat.h>

template <typename Stream>
static void SerializeBlockHeaderForHash(Stream& s, const CBlockHeader& block)
{
    ::Serialize(s, block.nVersion);
    ::Serialize(s, block.hashPrevBlock);
    ::Serialize(s, block.hashMerkleRoot);
    ::Serialize(s, block.nTime);
    ::Serialize(s, block.nBits);
    ::Serialize(s, block.nNonce);

    if (block.IsProofOfStake()) {
        ::Serialize(s, block.posStakeHash);
        ::Serialize(s, block.posStakeN);
    }
}

uint256 CBlockHeader::GetHash() const
{
    if (nVersion < 4)
    {
        uint256 thash;
        scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
        return thash;
    }
    return SerializeHash(*this);
}

uint256 CBlockHeader::GetPoWHash() const
{
    uint256 thash;
    scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
    return thash;
}

std::string CBlock::ToString() const
{
    std::stringstream s;

    if (IsProofOfStake()) {
         s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, posStakeHash=%s, posStakeN=%u, posPubKey=%u, posBlockSig=%u vtx=%u)\n",
                        GetHash().ToString(),
                        nVersion,
                        hashPrevBlock.ToString(),
                        hashMerkleRoot.ToString(),
                        nTime, nBits,
                        nNonce,
                        posStakeHash.ToString(),
                        posStakeN,
                        posPubKey.size(),
                        posBlockSig.size(),
                        vtx.size());
    }else{
        s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
            GetHash().ToString(),
            nVersion,
            hashPrevBlock.ToString(),
            hashMerkleRoot.ToString(),
            nTime, nBits, nNonce,
            vtx.size());
    }

    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

static void MarkVersionAsMostRecent(std::list<int32_t>& last_unique_versions, std::list<int32_t>::const_iterator version_it)
{
    if (version_it != last_unique_versions.cbegin()) {
        // Move the found version to the front of the list
        last_unique_versions.splice(last_unique_versions.begin(), last_unique_versions, version_it, std::next(version_it));
    }
}

static void SaveVersionAsMostRecent(std::list<int32_t>& last_unique_versions, const int32_t version)
{
    last_unique_versions.push_front(version);

    // Always keep the last 7 unique versions
    constexpr std::size_t max_backwards_look_ups = 7;
    if (last_unique_versions.size() > max_backwards_look_ups) {
        // Evict the oldest version
        last_unique_versions.pop_back();
    }
}

void CompressibleBlockHeader::Compress(const std::vector<CompressibleBlockHeader>& previous_blocks, std::list<int32_t>& last_unique_versions)
{
    if (previous_blocks.empty()) {
        // Previous block not available, we have to send the block completely uncompressed
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
        return;
    }

    // Try to compress version
    const auto version_it = std::find(last_unique_versions.cbegin(), last_unique_versions.cend(), nVersion);
    if (version_it != last_unique_versions.cend()) {
        // Version is found in the last 7 unique blocks.
        bit_field.SetVersionOffset(static_cast<uint8_t>(std::distance(last_unique_versions.cbegin(), version_it) + 1));

        // Mark the version as the most recent one
        MarkVersionAsMostRecent(last_unique_versions, version_it);
    } else {
        // Save the version as the most recent one
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
    }

    // Previous block is available
    const auto& last_block = previous_blocks.back();
    bit_field.MarkAsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH);

    // Compute compressed time diff
    const int64_t time_diff = nTime - last_block.nTime;
    if (time_diff <= std::numeric_limits<int16_t>::max() && time_diff >= std::numeric_limits<int16_t>::min()) {
        time_offset = static_cast<int16_t>(time_diff);
        bit_field.MarkAsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP);
    }

    // If n_bits matches previous block, it can be compressed (not sent at all)
    if (nBits == last_block.nBits) {
        bit_field.MarkAsCompressed(CompressedHeaderBitField::Flag::NBITS);
    }
}

void CompressibleBlockHeader::Uncompress(const std::vector<CBlockHeader>& previous_blocks, std::list<int32_t>& last_unique_versions)
{
    if (previous_blocks.empty()) {
        // First block in chain is always uncompressed
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
        return;
    }

    // We have the previous block
    const auto& last_block = previous_blocks.back();

    // Uncompress version
    if (bit_field.IsVersionCompressed()) {
        const auto version_offset = bit_field.GetVersionOffset();
        if (version_offset <= last_unique_versions.size()) {
            auto version_it = last_unique_versions.begin();
            std::advance(version_it, version_offset - 1);
            nVersion = *version_it;
            MarkVersionAsMostRecent(last_unique_versions, version_it);
        }
    } else {
        // Save the version as the most recent one
        SaveVersionAsMostRecent(last_unique_versions, nVersion);
    }

    // Uncompress prev block hash
    if (bit_field.IsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH)) {
        hashPrevBlock = last_block.GetHash();
    }

    // Uncompress timestamp
    if (bit_field.IsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP)) {
        nTime = last_block.nTime + time_offset;
    }

    // Uncompress n_bits
    if (bit_field.IsCompressed(CompressedHeaderBitField::Flag::NBITS)) {
        nBits = last_block.nBits;
    }
}

uint256 CBlockHeader::hashProofOfStake() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    SerializeBlockHeaderForHash(ss, *this);
    return ss.GetHash();
}

bool CBlockHeader::CheckBlockSignature(const CKeyID& key_id) const
{
    if (!IsProofOfStake()) {
        return true;
    }

    if (posBlockSig.empty()) {
        return false;
    }

    auto hash = GetHash();
    posPubKey.RecoverCompact(hash, posBlockSig);

    if (!posPubKey.IsValid()) {
        return false;
    }

    return posPubKey.GetID() == key_id;
}

const CPubKey& CBlockHeader::BlockPubKey() const
{
    // In case it's read from disk
    if (!posPubKey.IsValid() && !posBlockSig.empty()) {
        posPubKey.RecoverCompact(GetHash(), posBlockSig);
    }

    return posPubKey;
}

bool CBlock::HasCoinBase() const
{
    return (!vtx.empty() && CoinBase()->IsCoinBase());
}

bool CBlock::HasStake() const
{
    if (!IsProofOfStake() || (vtx.size() < 2)) {
        return false;
    }

    BlockPubKey();

    if (!posPubKey.IsValid()) {
        return false;
    }

    const auto spk = GetScriptForDestination(PKHash(posPubKey));
    const auto& cb_vout = CoinBase()->vout;
    const auto& stake = Stake();

    if (cb_vout.empty() || stake->vin.empty() || stake->vout.empty()) {
        return false;
    }

    if (stake->vin[0].prevout != COutPoint(posStakeHash, posStakeN)) {
        return false;
    }

    if (cb_vout[0].scriptPubKey != spk) {
        return false;
    }

    CAmount total_amt = 0;

    for (const auto& so : stake->vout) {
        if (so.IsEmpty()) {
            continue;
        }
        if (so.scriptPubKey != spk) {
            return false;
        }

        total_amt += so.nValue;
    }

    if (total_amt < MIN_STAKE_AMOUNT) {
        return false;
    }

    return true;
}
