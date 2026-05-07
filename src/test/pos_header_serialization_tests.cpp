// Copyright (c) 2024-2026 The Cosanta Core developers
// Copyright (c) 2026 The PirateCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Block header correctness, serialization, and disk I/O tests for PoW/PoS
//
// These tests verify:
//   1. CBlockHeader serialization differs between PoW and PoS
//   2. PoS headers include posStakeHash, posStakeN, posBlockSig in serialized form
//   3. PoW headers do NOT include PoS fields
//   4. CompressibleBlockHeader compression/decompression with PoS flag
//   5. CDiskBlockIndex serialization round-trip for PoW and PoS
//   6. Block hash computation differs between PoW (X11) and PoS (hashProofOfStake)
//   7. GetBlockHeader() preserves all fields
//   8. CompressedHeaderBitField flag management
//

#include <boost/test/unit_test.hpp>

#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <pos_kernel.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <random.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <version.h>

#include <list>

BOOST_FIXTURE_TEST_SUITE(pos_header_serialization_tests, BasicTestingSetup)

// ============================================================================
// 1. PoW vs PoS HEADER SERIALIZATION SIZE
// ============================================================================

BOOST_AUTO_TEST_CASE(pow_header_serialization_excludes_pos_fields)
{
    // A PoW header should serialize WITHOUT posStakeHash, posStakeN, posBlockSig
    CBlockHeader powHeader;
    powHeader.nVersion = 1; // PoW
    powHeader.hashPrevBlock = InsecureRand256();
    powHeader.hashMerkleRoot = InsecureRand256();
    powHeader.nTime = 1000000;
    powHeader.nBits = 0x1d00ffff;
    powHeader.nNonce = 42;
    // PoS fields set but should NOT appear in serialized output
    powHeader.posStakeHash = InsecureRand256();
    powHeader.posStakeN = 5;
    powHeader.posBlockSig = {0xDE, 0xAD};

    BOOST_CHECK(powHeader.IsProofOfWork());

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << powHeader;

    // PoW header: nVersion(4) + hashPrevBlock(32) + hashMerkleRoot(32) +
    //             nTime(4) + nBits(4) + nNonce(4) = 80 bytes
    BOOST_CHECK_EQUAL(ss.size(), 80U);
}

BOOST_AUTO_TEST_CASE(pos_header_serialization_includes_pos_fields)
{
    // A PoS header should serialize WITH posStakeHash, posStakeN, posBlockSig
    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1; // PoS
    posHeader.hashPrevBlock = InsecureRand256();
    posHeader.hashMerkleRoot = InsecureRand256();
    posHeader.nTime = 1000000;
    posHeader.nBits = 0x207fffff;
    posHeader.nNonce = 0;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 3;
    posHeader.posBlockSig = {0x01, 0x02, 0x03, 0x04, 0x05};

    BOOST_CHECK(posHeader.IsProofOfStake());

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << posHeader;

    // PoS header: base(80) + posStakeHash(32) + posStakeN(4) +
    //             posBlockSig(compactSize + data) = 80 + 32 + 4 + (1 + 5) = 122
    BOOST_CHECK_EQUAL(ss.size(), 80U + 32U + 4U + 1U + 5U);
}

BOOST_AUTO_TEST_CASE(pos_header_empty_sig_serialization)
{
    // PoS header with empty signature — still has PoS fields but sig is compact-size 0
    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1;
    posHeader.hashPrevBlock = InsecureRand256();
    posHeader.hashMerkleRoot = InsecureRand256();
    posHeader.nTime = 2000000;
    posHeader.nBits = 0x207fffff;
    posHeader.nNonce = 0;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 0;
    posHeader.posBlockSig.clear(); // Empty

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << posHeader;

    // 80 + 32 + 4 + 1(compact size 0) = 117
    BOOST_CHECK_EQUAL(ss.size(), 80U + 32U + 4U + 1U);
}

// ============================================================================
// 2. SERIALIZATION ROUND-TRIP: PoW
// ============================================================================

BOOST_AUTO_TEST_CASE(pow_header_serialize_deserialize_roundtrip)
{
    CBlockHeader original;
    original.nVersion = 0x20000001;
    original.hashPrevBlock = InsecureRand256();
    original.hashMerkleRoot = InsecureRand256();
    original.nTime = 1618221600;
    original.nBits = 0x1d00ffff;
    original.nNonce = 987654321;

    BOOST_CHECK(original.IsProofOfWork());

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << original;

    CBlockHeader deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.nVersion, original.nVersion);
    BOOST_CHECK_EQUAL(deserialized.hashPrevBlock, original.hashPrevBlock);
    BOOST_CHECK_EQUAL(deserialized.hashMerkleRoot, original.hashMerkleRoot);
    BOOST_CHECK_EQUAL(deserialized.nTime, original.nTime);
    BOOST_CHECK_EQUAL(deserialized.nBits, original.nBits);
    BOOST_CHECK_EQUAL(deserialized.nNonce, original.nNonce);

    // PoS fields should be default/empty for PoW
    BOOST_CHECK(deserialized.posStakeHash.IsNull());
    BOOST_CHECK_EQUAL(deserialized.posStakeN, 0U);
    BOOST_CHECK(deserialized.posBlockSig.empty());
}

// ============================================================================
// 3. SERIALIZATION ROUND-TRIP: PoS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_header_serialize_deserialize_roundtrip)
{
    CBlockHeader original;
    original.nVersion = CBlockHeader::POS_BIT | 0x01;
    original.hashPrevBlock = InsecureRand256();
    original.hashMerkleRoot = InsecureRand256();
    original.nTime = 1700000000;
    original.nBits = 0x207fffff;
    original.nNonce = 0; // stake modifier
    original.posStakeHash = InsecureRand256();
    original.posStakeN = 7;
    original.posBlockSig = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    BOOST_CHECK(original.IsProofOfStake());

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << original;

    CBlockHeader deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.nVersion, original.nVersion);
    BOOST_CHECK_EQUAL(deserialized.hashPrevBlock, original.hashPrevBlock);
    BOOST_CHECK_EQUAL(deserialized.hashMerkleRoot, original.hashMerkleRoot);
    BOOST_CHECK_EQUAL(deserialized.nTime, original.nTime);
    BOOST_CHECK_EQUAL(deserialized.nBits, original.nBits);
    BOOST_CHECK_EQUAL(deserialized.nNonce, original.nNonce);

    // PoS-specific fields must survive round-trip
    BOOST_CHECK_EQUAL(deserialized.posStakeHash, original.posStakeHash);
    BOOST_CHECK_EQUAL(deserialized.posStakeN, original.posStakeN);
    BOOST_CHECK(deserialized.posBlockSig == original.posBlockSig);
}

BOOST_AUTO_TEST_CASE(posv2_header_serialize_deserialize_roundtrip)
{
    // PoS v2 headers use POSV2_BITS
    CBlockHeader original;
    original.nVersion = CBlockHeader::POSV2_BITS | 0x01;
    original.hashPrevBlock = InsecureRand256();
    original.hashMerkleRoot = InsecureRand256();
    original.nTime = 1800000000;
    original.nBits = 0x207fffff;
    original.nNonce = 42;
    original.posStakeHash = InsecureRand256();
    original.posStakeN = 2;
    original.posBlockSig = {0x01, 0x02, 0x03};

    BOOST_CHECK(original.IsProofOfStake());
    BOOST_CHECK(original.IsProofOfStakeV2());

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << original;

    CBlockHeader deserialized;
    ss >> deserialized;

    BOOST_CHECK(deserialized.IsProofOfStake());
    BOOST_CHECK(deserialized.IsProofOfStakeV2());
    BOOST_CHECK_EQUAL(deserialized.posStakeHash, original.posStakeHash);
    BOOST_CHECK_EQUAL(deserialized.posStakeN, original.posStakeN);
    BOOST_CHECK(deserialized.posBlockSig == original.posBlockSig);
}

// ============================================================================
// 4. CBlock SERIALIZATION ROUND-TRIP
// ============================================================================

BOOST_AUTO_TEST_CASE(pow_block_serialize_deserialize_roundtrip)
{
    CBlock original;
    original.nVersion = 1;
    original.hashPrevBlock = InsecureRand256();
    original.nTime = 1618221600;
    original.nBits = 0x207fffff;
    original.nNonce = 12345;

    // Add a coinbase tx
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    original.vtx.push_back(MakeTransactionRef(std::move(coinbase)));

    original.hashMerkleRoot = BlockMerkleRoot(original);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << original;

    CBlock deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.nVersion, original.nVersion);
    BOOST_CHECK_EQUAL(deserialized.hashPrevBlock, original.hashPrevBlock);
    BOOST_CHECK_EQUAL(deserialized.hashMerkleRoot, original.hashMerkleRoot);
    BOOST_CHECK_EQUAL(deserialized.nTime, original.nTime);
    BOOST_CHECK_EQUAL(deserialized.nBits, original.nBits);
    BOOST_CHECK_EQUAL(deserialized.vtx.size(), original.vtx.size());
    BOOST_CHECK_EQUAL(deserialized.vtx[0]->GetHash(), original.vtx[0]->GetHash());
}

BOOST_AUTO_TEST_CASE(pos_block_serialize_deserialize_roundtrip)
{
    CBlock original;
    original.nVersion = CBlockHeader::POS_BIT | 1;
    original.hashPrevBlock = InsecureRand256();
    original.nTime = 1700000000;
    original.nBits = 0x207fffff;
    original.nNonce = 0;
    original.posStakeHash = InsecureRand256();
    original.posStakeN = 1;
    original.posBlockSig = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    // Add coinbase
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 0;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    original.vtx.push_back(MakeTransactionRef(std::move(coinbase)));

    // Add stake tx (coinstake)
    CMutableTransaction stake;
    stake.vin.resize(1);
    stake.vin[0].prevout = COutPoint(original.posStakeHash, original.posStakeN);
    stake.vout.resize(2);
    stake.vout[0].nValue = 0;
    stake.vout[0].scriptPubKey.clear();
    stake.vout[1].nValue = 10 * COIN;
    stake.vout[1].scriptPubKey = CScript() << OP_TRUE;
    original.vtx.push_back(MakeTransactionRef(std::move(stake)));

    original.hashMerkleRoot = BlockMerkleRoot(original);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << original;

    CBlock deserialized;
    ss >> deserialized;

    BOOST_CHECK(deserialized.IsProofOfStake());
    BOOST_CHECK_EQUAL(deserialized.posStakeHash, original.posStakeHash);
    BOOST_CHECK_EQUAL(deserialized.posStakeN, original.posStakeN);
    BOOST_CHECK(deserialized.posBlockSig == original.posBlockSig);
    BOOST_CHECK_EQUAL(deserialized.vtx.size(), 2U);
    BOOST_CHECK_EQUAL(deserialized.hashMerkleRoot, original.hashMerkleRoot);
}

// ============================================================================
// 5. GetBlockHeader() PRESERVES ALL FIELDS
// ============================================================================

BOOST_AUTO_TEST_CASE(get_block_header_preserves_pow_fields)
{
    CBlock block;
    block.nVersion = 0x20000001;
    block.hashPrevBlock = InsecureRand256();
    block.hashMerkleRoot = InsecureRand256();
    block.nTime = 1618221600;
    block.nBits = 0x1d00ffff;
    block.nNonce = 999;

    CBlockHeader header = block.GetBlockHeader();

    BOOST_CHECK_EQUAL(header.nVersion, block.nVersion);
    BOOST_CHECK_EQUAL(header.hashPrevBlock, block.hashPrevBlock);
    BOOST_CHECK_EQUAL(header.hashMerkleRoot, block.hashMerkleRoot);
    BOOST_CHECK_EQUAL(header.nTime, block.nTime);
    BOOST_CHECK_EQUAL(header.nBits, block.nBits);
    BOOST_CHECK_EQUAL(header.nNonce, block.nNonce);
}

BOOST_AUTO_TEST_CASE(get_block_header_preserves_pos_fields)
{
    CBlock block;
    block.nVersion = CBlockHeader::POS_BIT | 1;
    block.hashPrevBlock = InsecureRand256();
    block.hashMerkleRoot = InsecureRand256();
    block.nTime = 1700000000;
    block.nBits = 0x207fffff;
    block.nNonce = 0;
    block.posStakeHash = InsecureRand256();
    block.posStakeN = 42;
    block.posBlockSig = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

    CBlockHeader header = block.GetBlockHeader();

    BOOST_CHECK_EQUAL(header.posStakeHash, block.posStakeHash);
    BOOST_CHECK_EQUAL(header.posStakeN, block.posStakeN);
    BOOST_CHECK(header.posBlockSig == block.posBlockSig);
}

// ============================================================================
// 6. BLOCK HASH COMPUTATION: PoW vs PoS
// ============================================================================

BOOST_AUTO_TEST_CASE(pow_block_hash_uses_x11)
{
    // PoW block hash is computed via X11
    CBlockHeader powHeader;
    powHeader.nVersion = 1;
    powHeader.hashPrevBlock = InsecureRand256();
    powHeader.hashMerkleRoot = InsecureRand256();
    powHeader.nTime = 1618221600;
    powHeader.nBits = 0x207fffff;
    powHeader.nNonce = 42;

    BOOST_CHECK(powHeader.IsProofOfWork());

    uint256 hash = powHeader.GetHash();
    BOOST_CHECK(!hash.IsNull());

    // Same header should produce same hash
    uint256 hash2 = powHeader.GetHash();
    BOOST_CHECK_EQUAL(hash, hash2);
}

BOOST_AUTO_TEST_CASE(pos_block_hash_uses_proof_of_stake_hash)
{
    // PoS block hash uses hashProofOfStake() — includes posStakeHash & posStakeN
    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1;
    posHeader.hashPrevBlock = InsecureRand256();
    posHeader.hashMerkleRoot = InsecureRand256();
    posHeader.nTime = 1700000000;
    posHeader.nBits = 0x207fffff;
    posHeader.nNonce = 0;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 3;

    BOOST_CHECK(posHeader.IsProofOfStake());

    uint256 hash = posHeader.GetHash();
    BOOST_CHECK(!hash.IsNull());

    // Changing posStakeHash should change the hash
    CBlockHeader modified = posHeader;
    modified.posStakeHash = InsecureRand256();
    BOOST_CHECK(modified.GetHash() != hash);
}

BOOST_AUTO_TEST_CASE(pos_block_hash_changes_with_stake_n)
{
    // Changing posStakeN should produce a different hash
    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1;
    posHeader.hashPrevBlock = InsecureRand256();
    posHeader.hashMerkleRoot = InsecureRand256();
    posHeader.nTime = 1700000000;
    posHeader.nBits = 0x207fffff;
    posHeader.nNonce = 0;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 0;

    uint256 hash1 = posHeader.GetHash();

    posHeader.posStakeN = 1;
    uint256 hash2 = posHeader.GetHash();

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(pow_hash_not_affected_by_pos_fields)
{
    // For PoW blocks, changing posStakeHash should NOT change the hash
    CBlockHeader powHeader;
    powHeader.nVersion = 1;
    powHeader.hashPrevBlock = InsecureRand256();
    powHeader.hashMerkleRoot = InsecureRand256();
    powHeader.nTime = 1618221600;
    powHeader.nBits = 0x207fffff;
    powHeader.nNonce = 42;
    powHeader.posStakeHash.SetNull();

    uint256 hash1 = powHeader.GetHash();

    powHeader.posStakeHash = InsecureRand256();
    uint256 hash2 = powHeader.GetHash();

    // PoW hash ignores PoS fields
    BOOST_CHECK_EQUAL(hash1, hash2);
}

// ============================================================================
// 7. COMPRESSED HEADER BIT FIELD
// ============================================================================

BOOST_AUTO_TEST_CASE(compressed_header_bitfield_default)
{
    CompressedHeaderBitField bf;

    // Default: all fields are "compressed" (zero bits)
    BOOST_CHECK(bf.IsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH));
    BOOST_CHECK(bf.IsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP));
    BOOST_CHECK(bf.IsCompressed(CompressedHeaderBitField::Flag::NBITS));
    BOOST_CHECK(!bf.IsProofOfStake());
}

BOOST_AUTO_TEST_CASE(compressed_header_bitfield_pos_flag)
{
    CompressedHeaderBitField bf;

    bf.SetProofOfStake(true);
    BOOST_CHECK(bf.IsProofOfStake());

    bf.SetProofOfStake(false);
    BOOST_CHECK(!bf.IsProofOfStake());
}

BOOST_AUTO_TEST_CASE(compressed_header_bitfield_mark_uncompressed)
{
    CompressedHeaderBitField bf;

    bf.MarkAsUncompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH);
    BOOST_CHECK(!bf.IsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH));
    BOOST_CHECK(bf.IsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP));
    BOOST_CHECK(bf.IsCompressed(CompressedHeaderBitField::Flag::NBITS));

    bf.MarkAsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH);
    BOOST_CHECK(bf.IsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH));
}

BOOST_AUTO_TEST_CASE(compressed_header_bitfield_version_offset)
{
    CompressedHeaderBitField bf;

    BOOST_CHECK_EQUAL(bf.GetVersionOffset(), 0);
    BOOST_CHECK(!bf.IsVersionCompressed());

    bf.SetVersionOffset(3);
    BOOST_CHECK_EQUAL(bf.GetVersionOffset(), 3);
    BOOST_CHECK(bf.IsVersionCompressed());

    bf.SetVersionOffset(0);
    BOOST_CHECK(!bf.IsVersionCompressed());
}

BOOST_AUTO_TEST_CASE(compressed_header_bitfield_serialize_roundtrip)
{
    CompressedHeaderBitField original;
    original.SetProofOfStake(true);
    original.MarkAsUncompressed(CompressedHeaderBitField::Flag::TIMESTAMP);
    original.SetVersionOffset(5);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << original;

    CompressedHeaderBitField deserialized;
    ss >> deserialized;

    BOOST_CHECK(deserialized.IsProofOfStake());
    BOOST_CHECK(!deserialized.IsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP));
    BOOST_CHECK_EQUAL(deserialized.GetVersionOffset(), 5);
}

// ============================================================================
// 8. CompressibleBlockHeader COMPRESSION / DECOMPRESSION
// ============================================================================

BOOST_AUTO_TEST_CASE(compressible_header_pow_compress_decompress)
{
    // Create a PoW header and compress/decompress it
    CBlockHeader rawHeader;
    rawHeader.nVersion = 0x20000001;
    rawHeader.hashPrevBlock = InsecureRand256();
    rawHeader.hashMerkleRoot = InsecureRand256();
    rawHeader.nTime = 1618221600;
    rawHeader.nBits = 0x207fffff;
    rawHeader.nNonce = 42;

    CompressibleBlockHeader header(std::move(rawHeader));

    // First block (no previous): should be uncompressed
    std::vector<CompressibleBlockHeader> prevBlocks;
    std::list<int32_t> versions;
    header.Compress(prevBlocks, versions);

    // Serialize compressed
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << header;

    // Deserialize and decompress
    CompressibleBlockHeader decompressed;
    ss >> decompressed;

    std::vector<CBlockHeader> prevHeaders;
    std::list<int32_t> versions2;
    decompressed.Uncompress(prevHeaders, versions2);

    BOOST_CHECK_EQUAL(decompressed.nVersion, header.nVersion);
    BOOST_CHECK_EQUAL(decompressed.hashMerkleRoot, header.hashMerkleRoot);
    BOOST_CHECK(!decompressed.bit_field.IsProofOfStake());
}

BOOST_AUTO_TEST_CASE(compressible_header_pos_compress_decompress)
{
    // Create a PoS header and verify compression preserves PoS fields
    CBlockHeader rawHeader;
    rawHeader.nVersion = CBlockHeader::POS_BIT | 1;
    rawHeader.hashPrevBlock = InsecureRand256();
    rawHeader.hashMerkleRoot = InsecureRand256();
    rawHeader.nTime = 1700000000;
    rawHeader.nBits = 0x207fffff;
    rawHeader.nNonce = 0;
    rawHeader.posStakeHash = InsecureRand256();
    rawHeader.posStakeN = 5;
    rawHeader.posBlockSig = {0xAB, 0xCD, 0xEF};

    CompressibleBlockHeader header(std::move(rawHeader));

    // First block
    std::vector<CompressibleBlockHeader> prevBlocks;
    std::list<int32_t> versions;
    header.Compress(prevBlocks, versions);

    BOOST_CHECK(header.bit_field.IsProofOfStake());

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << header;

    CompressibleBlockHeader decompressed;
    ss >> decompressed;

    std::vector<CBlockHeader> prevHeaders;
    std::list<int32_t> versions2;
    decompressed.Uncompress(prevHeaders, versions2);

    BOOST_CHECK(decompressed.bit_field.IsProofOfStake());
    BOOST_CHECK_EQUAL(decompressed.posStakeHash, header.posStakeHash);
    BOOST_CHECK_EQUAL(decompressed.posStakeN, header.posStakeN);
    BOOST_CHECK(decompressed.posBlockSig == header.posBlockSig);
}

BOOST_AUTO_TEST_CASE(compressible_header_v1_pos_marker_only_in_nflags_decompress)
{
    CBlockHeader rawHeader;
    rawHeader.nVersion = 1; // no POS_BIT, no POSV2_BITS
    rawHeader.hashPrevBlock = InsecureRand256();
    rawHeader.hashMerkleRoot = InsecureRand256();
    rawHeader.nTime = 1600000000;
    rawHeader.nBits = 0x207fffff;
    rawHeader.nNonce = 0;
    rawHeader.posStakeHash = InsecureRand256();
    rawHeader.posStakeN = 7;
    rawHeader.posBlockSig = {0xCA, 0xFE, 0xBA, 0xBE};
    rawHeader.nFlags = CBlockIndex::BLOCK_PROOF_OF_STAKE;

    BOOST_CHECK(rawHeader.IsProofOfStake());
    BOOST_CHECK(!rawHeader.IsProofOfStakeV2());
    BOOST_CHECK_EQUAL(rawHeader.nVersion & CBlockHeader::POS_BIT, 0U);

    CompressibleBlockHeader header(std::move(rawHeader));
    BOOST_CHECK(header.bit_field.IsProofOfStake());

    std::vector<CompressibleBlockHeader> prevBlocks;
    std::list<int32_t> versions;
    header.Compress(prevBlocks, versions);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << header;

    CompressibleBlockHeader decompressed;
    ss >> decompressed;
    BOOST_CHECK_EQUAL(decompressed.nFlags, 0U); // nFlags is not on the wire

    std::vector<CBlockHeader> prevHeaders;
    std::list<int32_t> versions2;
    decompressed.Uncompress(prevHeaders, versions2);

    BOOST_CHECK(decompressed.bit_field.IsProofOfStake());
    BOOST_CHECK(decompressed.IsProofOfStake());
    BOOST_CHECK(!decompressed.IsProofOfStakeV2());
    BOOST_CHECK_EQUAL(decompressed.nFlags & CBlockIndex::BLOCK_PROOF_OF_STAKE,
                      static_cast<uint32_t>(CBlockIndex::BLOCK_PROOF_OF_STAKE));

    BOOST_CHECK_EQUAL(decompressed.nVersion, header.nVersion);
    BOOST_CHECK_EQUAL(decompressed.posStakeHash, header.posStakeHash);
    BOOST_CHECK_EQUAL(decompressed.posStakeN, header.posStakeN);
    BOOST_CHECK(decompressed.posBlockSig == header.posBlockSig);
}

BOOST_AUTO_TEST_CASE(compressible_header_prev_block_compression)
{
    // When previous block is available, hashPrevBlock should be compressed

    // First block (uncompressed)
    CBlockHeader raw1;
    raw1.nVersion = 1;
    raw1.hashPrevBlock.SetNull();
    raw1.hashMerkleRoot = InsecureRand256();
    raw1.nTime = 1000;
    raw1.nBits = 0x207fffff;
    raw1.nNonce = 1;

    CompressibleBlockHeader h1(std::move(raw1));
    std::vector<CompressibleBlockHeader> prevBlocks;
    std::list<int32_t> versions;
    h1.Compress(prevBlocks, versions);
    prevBlocks.push_back(h1);

    // Second block
    CBlockHeader raw2;
    raw2.nVersion = 1;
    raw2.hashPrevBlock = h1.GetHash();
    raw2.hashMerkleRoot = InsecureRand256();
    raw2.nTime = 1060; // 60 seconds later
    raw2.nBits = 0x207fffff; // Same as prev
    raw2.nNonce = 2;

    CompressibleBlockHeader h2(std::move(raw2));
    h2.Compress(prevBlocks, versions);

    // hashPrevBlock should be compressed (inferred from previous)
    BOOST_CHECK(h2.bit_field.IsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH));
    // nBits matches prev, should be compressed
    BOOST_CHECK(h2.bit_field.IsCompressed(CompressedHeaderBitField::Flag::NBITS));
    // Time diff 60 fits in int16_t, should be compressed
    BOOST_CHECK(h2.bit_field.IsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP));
}

BOOST_AUTO_TEST_CASE(compressible_header_nbits_change_not_compressed)
{
    // When nBits changes, it should NOT be compressed

    CBlockHeader raw1;
    raw1.nVersion = 1;
    raw1.hashMerkleRoot = InsecureRand256();
    raw1.nTime = 1000;
    raw1.nBits = 0x207fffff;
    raw1.nNonce = 1;

    CompressibleBlockHeader h1(std::move(raw1));
    std::vector<CompressibleBlockHeader> prevBlocks;
    std::list<int32_t> versions;
    h1.Compress(prevBlocks, versions);
    prevBlocks.push_back(h1);

    CBlockHeader raw2;
    raw2.nVersion = 1;
    raw2.hashMerkleRoot = InsecureRand256();
    raw2.nTime = 1060;
    raw2.nBits = 0x1d00ffff; // DIFFERENT from prev
    raw2.nNonce = 2;

    CompressibleBlockHeader h2(std::move(raw2));
    h2.Compress(prevBlocks, versions);

    // nBits changed — must NOT be compressed
    BOOST_CHECK(!h2.bit_field.IsCompressed(CompressedHeaderBitField::Flag::NBITS));
}

// ============================================================================
// 9. CDiskBlockIndex SERIALIZATION
// ============================================================================

BOOST_AUTO_TEST_CASE(disk_block_index_pow_roundtrip)
{
    // CDiskBlockIndex for PoW block should serialize and deserialize correctly
    uint256 blockHash = InsecureRand256();
    CBlockIndex index;
    index.phashBlock = &blockHash;
    index.nHeight = 500;
    index.nStatus = BLOCK_HAVE_DATA;
    index.nTx = 10;
    index.nFile = 1;
    index.nDataPos = 1234;
    index.nVersion = 0x20000001; // PoW
    index.hashMerkleRoot = InsecureRand256();
    index.nTime = 1618221600;
    index.nBits = 0x1d00ffff;
    index.nNonce = 42;

    CDiskBlockIndex diskIndex(&index);
    diskIndex.hashPrev = InsecureRand256();

    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << diskIndex;

    CDiskBlockIndex deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.nHeight, index.nHeight);
    BOOST_CHECK_EQUAL(deserialized.nVersion, index.nVersion);
    BOOST_CHECK_EQUAL(deserialized.hashMerkleRoot, index.hashMerkleRoot);
    BOOST_CHECK_EQUAL(deserialized.nTime, index.nTime);
    BOOST_CHECK_EQUAL(deserialized.nBits, index.nBits);
    BOOST_CHECK_EQUAL(deserialized.nNonce, index.nNonce);
    BOOST_CHECK_EQUAL(deserialized.hash, blockHash);
    BOOST_CHECK_EQUAL(deserialized.hashPrev, diskIndex.hashPrev);

    // PoW: no PoS fields
    BOOST_CHECK(deserialized.IsProofOfWork());
    BOOST_CHECK(deserialized.posStakeHash.IsNull());
}

BOOST_AUTO_TEST_CASE(disk_block_index_pos_roundtrip)
{
    // CDiskBlockIndex for PoS block should include PoS fields
    uint256 blockHash = InsecureRand256();
    CBlockIndex index;
    index.phashBlock = &blockHash;
    index.nHeight = 100001;
    index.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    index.nTx = 5;
    index.nFile = 2;
    index.nDataPos = 5678;
    index.nUndoPos = 9012;
    index.nVersion = CBlockHeader::POS_BIT | 1; // PoS
    index.hashMerkleRoot = InsecureRand256();
    index.nTime = 1700000000;
    index.nBits = 0x207fffff;
    index.nNonce = 0;
    index.posStakeHash = InsecureRand256();
    index.posStakeN = 3;
    index.posBlockSig = {0xDE, 0xAD, 0xBE, 0xEF};

    CDiskBlockIndex diskIndex(&index);
    diskIndex.hashPrev = InsecureRand256();

    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << diskIndex;

    CDiskBlockIndex deserialized;
    ss >> deserialized;

    BOOST_CHECK(deserialized.IsProofOfStake());
    BOOST_CHECK_EQUAL(deserialized.nHeight, index.nHeight);
    BOOST_CHECK_EQUAL(deserialized.posStakeHash, index.posStakeHash);
    BOOST_CHECK_EQUAL(deserialized.posStakeN, index.posStakeN);
    BOOST_CHECK(deserialized.posBlockSig == index.posBlockSig);
    BOOST_CHECK_EQUAL(deserialized.nUndoPos, index.nUndoPos);
}

// ============================================================================
// 10. NEGATIVE: CORRUPTED SERIALIZATION
// ============================================================================

BOOST_AUTO_TEST_CASE(truncated_pos_header_deserialization_fails)
{
    // If a PoS header is truncated (missing PoS fields), deserialization
    // should fail

    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1;
    posHeader.hashPrevBlock = InsecureRand256();
    posHeader.hashMerkleRoot = InsecureRand256();
    posHeader.nTime = 1700000000;
    posHeader.nBits = 0x207fffff;
    posHeader.nNonce = 0;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 1;
    posHeader.posBlockSig = {0x01, 0x02, 0x03};

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << posHeader;

    // Truncate: remove the last few bytes (part of posBlockSig)
    std::vector<char> data(ss.begin(), ss.end());
    data.resize(data.size() - 2); // Remove 2 bytes

    CDataStream truncated(data, SER_NETWORK, PROTOCOL_VERSION);
    CBlockHeader deserialized;

    bool threw = false;
    try {
        truncated >> deserialized;
    } catch (const std::ios_base::failure&) {
        threw = true;
    }

    BOOST_CHECK_MESSAGE(threw, "Truncated PoS header should fail deserialization");
}

BOOST_AUTO_TEST_CASE(pow_header_with_extra_data_ok)
{
    // PoW header followed by extra data — deserialization should succeed
    // (extra data is just ignored/left in stream)

    CBlockHeader powHeader;
    powHeader.nVersion = 1;
    powHeader.hashPrevBlock = InsecureRand256();
    powHeader.hashMerkleRoot = InsecureRand256();
    powHeader.nTime = 1618221600;
    powHeader.nBits = 0x207fffff;
    powHeader.nNonce = 42;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << powHeader;

    // Add extra garbage at end
    ss << uint32_t(0xDEADBEEF);

    CBlockHeader deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.nVersion, powHeader.nVersion);
    BOOST_CHECK_EQUAL(deserialized.nNonce, powHeader.nNonce);
    // Extra data should remain in the stream
    BOOST_CHECK(!ss.empty());
}

// ============================================================================
// 11. HEADER SetNull / IsNull
// ============================================================================

BOOST_AUTO_TEST_CASE(header_set_null_clears_all_fields)
{
    CBlockHeader header;
    header.nVersion = CBlockHeader::POS_BIT | 1;
    header.hashPrevBlock = InsecureRand256();
    header.hashMerkleRoot = InsecureRand256();
    header.nTime = 1700000000;
    header.nBits = 0x207fffff;
    header.nNonce = 42;
    header.posStakeHash = InsecureRand256();
    header.posStakeN = 7;
    header.posBlockSig = {0x01, 0x02};

    header.SetNull();

    BOOST_CHECK_EQUAL(header.nVersion, 0);
    BOOST_CHECK(header.hashPrevBlock.IsNull());
    BOOST_CHECK(header.hashMerkleRoot.IsNull());
    BOOST_CHECK_EQUAL(header.nTime, 0U);
    BOOST_CHECK_EQUAL(header.nBits, 0U);
    BOOST_CHECK_EQUAL(header.nNonce, 0U);
    BOOST_CHECK(header.posStakeHash.IsNull());
    BOOST_CHECK_EQUAL(header.posStakeN, 0U);
    BOOST_CHECK(header.posBlockSig.empty());
    BOOST_CHECK(header.IsNull()); // nBits == 0
}

BOOST_AUTO_TEST_CASE(header_is_null_depends_on_nbits)
{
    CBlockHeader header;
    header.SetNull();
    BOOST_CHECK(header.IsNull());

    header.nBits = 1;
    BOOST_CHECK(!header.IsNull());

    header.nBits = 0;
    BOOST_CHECK(header.IsNull());
}

// ============================================================================
// 12. nStakeModifier() ALIAS FOR nNonce
// ============================================================================

BOOST_AUTO_TEST_CASE(stake_modifier_is_nonce_alias)
{
    // In PoS blocks, nNonce is repurposed as the stake modifier.
    // nStakeModifier() should reference the same memory as nNonce.

    CBlockHeader header;
    header.nNonce = 0x12345678;

    BOOST_CHECK_EQUAL(header.nStakeModifier(), 0x12345678U);

    header.nStakeModifier() = 0xABCDEF01;
    BOOST_CHECK_EQUAL(header.nNonce, 0xABCDEF01U);
}

// ============================================================================
// 13. VERSION BITS CORRECTNESS
// ============================================================================

BOOST_AUTO_TEST_CASE(version_bits_pos_identification)
{
    // POS_BIT is 0x10000000
    BOOST_CHECK_EQUAL(CBlockHeader::POS_BIT, 0x10000000UL);

    // POSV2_BITS is POS_BIT | 0x08000000 = 0x18000000
    BOOST_CHECK_EQUAL(CBlockHeader::POSV2_BITS, 0x18000000UL);

    // A version with POSV2_BITS set should be both PoS and PoSv2
    CBlockHeader header;
    header.nVersion = CBlockHeader::POSV2_BITS;
    BOOST_CHECK(header.IsProofOfStake());
    BOOST_CHECK(header.IsProofOfStakeV2());

    // A version with only POS_BIT should be PoS but not PoSv2
    header.nVersion = CBlockHeader::POS_BIT;
    BOOST_CHECK(header.IsProofOfStake());
    BOOST_CHECK(!header.IsProofOfStakeV2());

    // A version without POS_BIT should be PoW
    header.nVersion = 0x08000000; // Only the v2 sub-bit, NOT POS_BIT
    BOOST_CHECK(header.IsProofOfWork());
    BOOST_CHECK(!header.IsProofOfStake());
}

// ============================================================================
// 14. SERIALIZATION SIZE COMPARISON
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_serialization_always_larger_than_pow)
{
    // PoS headers should always serialize to a larger size than PoW
    // because they include posStakeHash + posStakeN + posBlockSig

    CBlockHeader powHeader;
    powHeader.nVersion = 1;
    powHeader.hashPrevBlock = InsecureRand256();
    powHeader.hashMerkleRoot = InsecureRand256();
    powHeader.nTime = 1618221600;
    powHeader.nBits = 0x207fffff;
    powHeader.nNonce = 42;

    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1;
    posHeader.hashPrevBlock = InsecureRand256();
    posHeader.hashMerkleRoot = InsecureRand256();
    posHeader.nTime = 1700000000;
    posHeader.nBits = 0x207fffff;
    posHeader.nNonce = 0;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 0;
    posHeader.posBlockSig.clear(); // Even with empty sig

    CDataStream ssPow(SER_NETWORK, PROTOCOL_VERSION);
    ssPow << powHeader;

    CDataStream ssPos(SER_NETWORK, PROTOCOL_VERSION);
    ssPos << posHeader;

    // PoS must be larger (at minimum: +32 hash + 4 n + 1 compactsize = +37)
    BOOST_CHECK_GT(ssPos.size(), ssPow.size());
    BOOST_CHECK_EQUAL(ssPos.size() - ssPow.size(), 37U); // 32+4+1
}

// ============================================================================
// 15. CompressibleBlockHeader FROM CBlockHeader CONSTRUCTOR
// ============================================================================

BOOST_AUTO_TEST_CASE(compressible_header_constructor_sets_flags)
{
    // PoW block
    CBlockHeader rawPow;
    rawPow.nVersion = 1;
    rawPow.nBits = 0x207fffff;

    CompressibleBlockHeader compPow(std::move(rawPow));
    BOOST_CHECK(!compPow.bit_field.IsProofOfStake());
    BOOST_CHECK(!compPow.bit_field.IsCompressed(CompressedHeaderBitField::Flag::PREV_BLOCK_HASH));
    BOOST_CHECK(!compPow.bit_field.IsCompressed(CompressedHeaderBitField::Flag::TIMESTAMP));
    BOOST_CHECK(!compPow.bit_field.IsCompressed(CompressedHeaderBitField::Flag::NBITS));

    // PoS block
    CBlockHeader rawPos;
    rawPos.nVersion = CBlockHeader::POS_BIT | 1;
    rawPos.nBits = 0x207fffff;
    rawPos.posStakeHash = InsecureRand256();

    CompressibleBlockHeader compPos(std::move(rawPos));
    BOOST_CHECK(compPos.bit_field.IsProofOfStake());
}

// ============================================================================
// 16. SER_GETHASH SERIALIZATION (hash-specific boundaries)
// ============================================================================

BOOST_AUTO_TEST_CASE(ser_gethash_pow_payload)
{
    // Verify that PoW header's SER_GETHASH serialization matches SER_NETWORK
    // (no PoS fields in either case for PoW)
    CBlockHeader powHeader;
    powHeader.nVersion = 1;
    powHeader.hashPrevBlock = InsecureRand256();
    powHeader.hashMerkleRoot = InsecureRand256();
    powHeader.nTime = 1618221600;
    powHeader.nBits = 0x207fffff;
    powHeader.nNonce = 42;

    CDataStream ssNet(SER_NETWORK, PROTOCOL_VERSION);
    ssNet << powHeader;

    CDataStream ssHash(SER_GETHASH, PROTOCOL_VERSION);
    ssHash << powHeader;

    // For PoW, both serializations should be identical (80 bytes)
    BOOST_CHECK_EQUAL(ssNet.size(), ssHash.size());
    BOOST_CHECK_EQUAL(ssNet.size(), 80U);

    // Byte-for-byte equality
    std::vector<char> netBytes(ssNet.begin(), ssNet.end());
    std::vector<char> hashBytes(ssHash.begin(), ssHash.end());
    BOOST_CHECK(netBytes == hashBytes);
}

BOOST_AUTO_TEST_CASE(ser_gethash_pos_payload)
{
    // Verify that PoS header's SER_GETHASH serialization includes PoS fields
    // just like SER_NETWORK does. This is critical for hash consistency —
    // if SER_GETHASH dropped PoS fields, hashes would break.
    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1;
    posHeader.hashPrevBlock = InsecureRand256();
    posHeader.hashMerkleRoot = InsecureRand256();
    posHeader.nTime = 1700000000;
    posHeader.nBits = 0x207fffff;
    posHeader.nNonce = 0;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 5;
    posHeader.posBlockSig = {0xAA, 0xBB, 0xCC};

    CDataStream ssNet(SER_NETWORK, PROTOCOL_VERSION);
    ssNet << posHeader;

    CDataStream ssHash(SER_GETHASH, PROTOCOL_VERSION);
    ssHash << posHeader;

    // For PoS, both serializations should be identical
    BOOST_CHECK_EQUAL(ssNet.size(), ssHash.size());

    std::vector<char> netBytes(ssNet.begin(), ssNet.end());
    std::vector<char> hashBytes(ssHash.begin(), ssHash.end());
    BOOST_CHECK(netBytes == hashBytes);

    // And both should include PoS fields (> 80 bytes)
    BOOST_CHECK_GT(ssHash.size(), 80U);
}

BOOST_AUTO_TEST_CASE(ser_gethash_pos_differs_from_pow_same_base)
{
    // Create PoW and PoS headers with identical base fields (except nVersion).
    // Their SER_GETHASH payloads MUST differ because PoS includes extra fields.
    // This catches regressions where SER_GETHASH silently drops PoS data.

    uint256 prevBlock = InsecureRand256();
    uint256 merkle = InsecureRand256();
    uint32_t time = 1700000000;
    uint32_t bits = 0x207fffff;
    uint32_t nonce = 42;

    CBlockHeader powHeader;
    powHeader.nVersion = 1;
    powHeader.hashPrevBlock = prevBlock;
    powHeader.hashMerkleRoot = merkle;
    powHeader.nTime = time;
    powHeader.nBits = bits;
    powHeader.nNonce = nonce;

    CBlockHeader posHeader;
    posHeader.nVersion = CBlockHeader::POS_BIT | 1;
    posHeader.hashPrevBlock = prevBlock;
    posHeader.hashMerkleRoot = merkle;
    posHeader.nTime = time;
    posHeader.nBits = bits;
    posHeader.nNonce = nonce;
    posHeader.posStakeHash = InsecureRand256();
    posHeader.posStakeN = 1;
    posHeader.posBlockSig = {0x01};

    CDataStream ssPow(SER_GETHASH, PROTOCOL_VERSION);
    ssPow << powHeader;

    CDataStream ssPos(SER_GETHASH, PROTOCOL_VERSION);
    ssPos << posHeader;

    // Sizes must differ
    BOOST_CHECK_GT(ssPos.size(), ssPow.size());

    // The first 4 bytes (nVersion) already differ, but more importantly
    // the PoS payload is longer
    std::vector<char> powBytes(ssPow.begin(), ssPow.end());
    std::vector<char> posBytes(ssPos.begin(), ssPos.end());
    BOOST_CHECK(powBytes != posBytes);
}

BOOST_AUTO_TEST_CASE(ser_gethash_hash_proof_of_stake_consistency)
{
    // hashProofOfStake() internally serializes via SerializeBlockHeaderForHash
    // which includes posStakeHash and posStakeN but NOT posBlockSig.
    // Verify that changing posBlockSig does NOT change hashProofOfStake(),
    // but changing posStakeHash or posStakeN DOES.

    CBlockHeader base;
    base.nVersion = CBlockHeader::POS_BIT | 1;
    base.hashPrevBlock = InsecureRand256();
    base.hashMerkleRoot = InsecureRand256();
    base.nTime = 1700000000;
    base.nBits = 0x207fffff;
    base.nNonce = 0;
    base.posStakeHash = InsecureRand256();
    base.posStakeN = 3;
    base.posBlockSig = {0x01, 0x02, 0x03};

    uint256 hash1 = base.hashProofOfStake();

    // Change posBlockSig: hash should NOT change (sig excluded from hash input)
    CBlockHeader withDifferentSig = base;
    withDifferentSig.posBlockSig = {0xFF, 0xFE, 0xFD};
    uint256 hash2 = withDifferentSig.hashProofOfStake();
    BOOST_CHECK_EQUAL(hash1, hash2);

    // Change posStakeHash: hash MUST change
    CBlockHeader withDifferentStake = base;
    withDifferentStake.posStakeHash = InsecureRand256();
    uint256 hash3 = withDifferentStake.hashProofOfStake();
    BOOST_CHECK(hash1 != hash3);

    // Change posStakeN: hash MUST change
    CBlockHeader withDifferentN = base;
    withDifferentN.posStakeN = base.posStakeN + 1;
    uint256 hash4 = withDifferentN.hashProofOfStake();
    BOOST_CHECK(hash1 != hash4);
}

BOOST_AUTO_TEST_CASE(ser_gethash_disk_block_index_pos_includes_pos_fields)
{
    // CDiskBlockIndex SER_DISK serialization for PoS must include PoS fields.
    // Compare PoW and PoS CDiskBlockIndex sizes.

    uint256 powHash = InsecureRand256();
    CBlockIndex powIndex;
    powIndex.phashBlock = &powHash;
    powIndex.nHeight = 100;
    powIndex.nStatus = BLOCK_HAVE_DATA;
    powIndex.nTx = 5;
    powIndex.nFile = 1;
    powIndex.nDataPos = 100;
    powIndex.nVersion = 1; // PoW
    powIndex.hashMerkleRoot = InsecureRand256();
    powIndex.nTime = 1618221600;
    powIndex.nBits = 0x207fffff;
    powIndex.nNonce = 42;

    CDiskBlockIndex powDisk(&powIndex);
    powDisk.hashPrev = InsecureRand256();

    uint256 posHash = InsecureRand256();
    CBlockIndex posIndex;
    posIndex.phashBlock = &posHash;
    posIndex.nHeight = 100;
    posIndex.nStatus = BLOCK_HAVE_DATA;
    posIndex.nTx = 5;
    posIndex.nFile = 1;
    posIndex.nDataPos = 100;
    posIndex.nVersion = CBlockHeader::POS_BIT | 1; // PoS
    posIndex.hashMerkleRoot = InsecureRand256();
    posIndex.nTime = 1700000000;
    posIndex.nBits = 0x207fffff;
    posIndex.nNonce = 0;
    posIndex.posStakeHash = InsecureRand256();
    posIndex.posStakeN = 3;
    posIndex.posBlockSig = {0x01, 0x02, 0x03};

    CDiskBlockIndex posDisk(&posIndex);
    posDisk.hashPrev = InsecureRand256();

    CDataStream ssPow(SER_DISK, PROTOCOL_VERSION);
    ssPow << powDisk;

    CDataStream ssPos(SER_DISK, PROTOCOL_VERSION);
    ssPos << posDisk;

    // PoS disk index must be larger due to PoS fields
    BOOST_CHECK_GT(ssPos.size(), ssPow.size());
}

BOOST_AUTO_TEST_SUITE_END()
