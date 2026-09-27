// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <pow.h>
#include <streams.h>
#include <txdb.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>
#include <test/util/setup_common.h>

using node::BlockManager;
using node::BLOCK_SERIALIZATION_HEADER_SIZE;
using node::MAX_BLOCKFILE_SIZE;
using node::OpenBlockFile;

// use BasicTestingSetup here for the data directory configuration, setup, and cleanup
BOOST_FIXTURE_TEST_SUITE(blockmanager_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(blockmanager_find_block_pos)
{
    const auto params {CreateChainParams(ArgsManager{}, CBaseChainParams::MAIN)};
    node::BlockManager::Options blockman_opts{
        .chainparams = *params,
    };
    BlockManager blockman{blockman_opts};
    // simulate adding a genesis block normally
    BOOST_CHECK_EQUAL(blockman.SaveBlockToDisk(params->GenesisBlock(), 0, nullptr).nPos, BLOCK_SERIALIZATION_HEADER_SIZE);
    // simulate what happens during reindex
    // simulate a well-formed genesis block being found at offset 8 in the blk00000.dat file
    // the block is found at offset 8 because there is an 8 byte serialization header
    // consisting of 4 magic bytes + 4 length bytes before each block in a well-formed blk file.
    FlatFilePos pos{0, BLOCK_SERIALIZATION_HEADER_SIZE};
    BOOST_CHECK_EQUAL(blockman.SaveBlockToDisk(params->GenesisBlock(), 0, &pos).nPos, BLOCK_SERIALIZATION_HEADER_SIZE);
    // now simulate what happens after reindex for the first new block processed
    // the actual block contents don't matter, just that it's a block.
    // verify that the write position is at offset 0x12d.
    // this is a check to make sure that https://github.com/bitcoin/bitcoin/issues/21379 does not recur
    // 8 bytes (for serialization header) + 285 (for serialized genesis block) = 293
    // add another 8 bytes for the second block's serialization header and we get 293 + 8 = 301
    FlatFilePos actual{blockman.SaveBlockToDisk(params->GenesisBlock(), 1, nullptr)};
    BOOST_CHECK_EQUAL(actual.nPos, BLOCK_SERIALIZATION_HEADER_SIZE + ::GetSerializeSize(params->GenesisBlock(), CLIENT_VERSION) + BLOCK_SERIALIZATION_HEADER_SIZE);
}

BOOST_AUTO_TEST_CASE(genesis_without_pow_read_from_disk)
{
    const auto params{CreateChainParams(*m_node.args, CBaseChainParams::MAIN)};
    const auto& consensus{params->GetConsensus()};
    CBlock genesis{params->GenesisBlock()};
    genesis.fChecked = false;
    genesis.m_checked_merkle_root = false;
    BOOST_REQUIRE_EQUAL(genesis.GetHash().ToString(), "33422d3f8e94bae7cd2544e737d64ff8ec3ee140cc3fdc4db3d14656f9a60912");
    BOOST_REQUIRE(!CheckProofOfWork(genesis.GetHash(), genesis.nBits, consensus));

    BlockValidationState genesis_state;
    BOOST_CHECK_MESSAGE(CheckBlock(genesis, genesis_state, consensus), genesis_state.ToString());

    BlockManager blockman{BlockManager::Options{.chainparams = *params}};
    const auto genesis_pos{blockman.SaveBlockToDisk(genesis, 0, nullptr)};
    BOOST_REQUIRE(!genesis_pos.IsNull());
    CBlock read_block;
    const auto read_hash{node::ReadBlockFromDisk(read_block, genesis_pos, consensus)};
    BOOST_REQUIRE(read_hash);
    BOOST_CHECK_EQUAL(*read_hash, genesis.GetHash());

    CBlock invalid{genesis};
    ++invalid.nTime;
    invalid.fChecked = false;
    invalid.m_checked_merkle_root = false;
    BOOST_REQUIRE(!CheckProofOfWork(invalid.GetHash(), invalid.nBits, consensus));
    BlockValidationState invalid_state;
    BOOST_CHECK(!CheckBlock(invalid, invalid_state, consensus));
    BOOST_CHECK_EQUAL(invalid_state.GetRejectReason(), "high-hash");

    const auto invalid_pos{blockman.SaveBlockToDisk(invalid, 1, nullptr)};
    BOOST_REQUIRE(!invalid_pos.IsNull());
    BOOST_CHECK(!node::ReadBlockFromDisk(read_block, invalid_pos, consensus));
}

BOOST_AUTO_TEST_CASE(genesis_without_pow_load_block_index)
{
    const auto params{CreateChainParams(*m_node.args, CBaseChainParams::MAIN)};
    const auto& consensus{params->GetConsensus()};
    const auto check_load = [&](const CBlock& block, bool expected) {
        const auto hash{block.GetHash()};
        BOOST_REQUIRE(!CheckProofOfWork(hash, block.nBits, consensus));
        BlockManager source{BlockManager::Options{.chainparams = *params}};
        BlockManager restored{BlockManager::Options{.chainparams = *params}};
        CBlockTreeDB block_tree_db{1 << 20, /*fMemory=*/true};
        LOCK(cs_main);
        CBlockIndex* best_header{nullptr};
        const auto* index{source.AddToBlockIndex(block, hash, best_header)};
        BOOST_REQUIRE(block_tree_db.WriteBatchSync({}, 0, {index}));
        const auto insert = [&](const uint256& block_hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
            return restored.InsertBlockIndex(block_hash);
        };
        BOOST_CHECK_EQUAL(block_tree_db.LoadBlockIndexGuts(consensus, insert), expected);
        if (expected) {
            const auto* loaded{restored.LookupBlockIndex(hash)};
            BOOST_REQUIRE(loaded);
            BOOST_CHECK_EQUAL(loaded->GetBlockHeader().GetHash(), hash);
        }
    };

    CBlock genesis{params->GenesisBlock()};
    BOOST_REQUIRE_EQUAL(genesis.GetHash(), consensus.hashGenesisBlock);
    check_load(genesis, true);
    ++genesis.nTime;
    genesis.fChecked = false;
    genesis.m_checked_merkle_root = false;
    check_load(genesis, false);
}

BOOST_AUTO_TEST_CASE(pos_metadata_load_block_index)
{
    const auto params{CreateChainParams(*m_node.args, CBaseChainParams::MAIN)};
    const auto& consensus{params->GetConsensus()};
    const unsigned int modifier_flags{CBlockIndex::BLOCK_STAKE_ENTROPY | CBlockIndex::BLOCK_STAKE_MODIFIER};
    const struct {
        int32_t version;
        unsigned int flags;
        bool proof_of_stake;
    } cases[]{
        {1, modifier_flags | CBlockIndex::BLOCK_PROOF_OF_STAKE, true},
        {CBlockHeader::POSV2_BITS | 4, modifier_flags, true},
        {1, modifier_flags, false},
    };

    for (const auto& test : cases) {
        BOOST_TEST_CONTEXT("version=" << test.version << ", flags=" << test.flags) {
            CBlockHeader header;
            header.nVersion = test.version;
            header.hashPrevBlock = consensus.hashGenesisBlock;
            header.hashMerkleRoot = uint256S("1234");
            header.nTime = params->GenesisBlock().nTime + 120;
            header.nBits = 0x1d00ffff;
            header.nNonce = 12345;
            header.nFlags = test.flags;
            header.posStakeHash = uint256S("5678");
            header.posStakeN = 2;
            header.posBlockSig = {0x30, 0x01, 0x02};
            const auto hash{header.GetHash()};
            BOOST_REQUIRE(hash != consensus.hashGenesisBlock);
            BOOST_REQUIRE(!CheckProofOfWork(header.GetPoWHash(), header.nBits, consensus));

            BlockManager source{BlockManager::Options{.chainparams = *params}};
            BlockManager restored{BlockManager::Options{.chainparams = *params}};
            CBlockTreeDB block_tree_db{1 << 20, /*fMemory=*/true};
            LOCK(cs_main);
            CBlockIndex* best_header{nullptr};
            source.AddToBlockIndex(params->GenesisBlock(), consensus.hashGenesisBlock, best_header);
            auto* index{source.AddToBlockIndex(header, hash, best_header)};
            BOOST_REQUIRE_EQUAL(index->nFlags, test.flags);
            BOOST_REQUIRE_EQUAL(index->IsProofOfStake(), test.proof_of_stake);
            index->nFile = 3;
            index->nDataPos = 80;
            index->nUndoPos = 120;
            index->nStatus |= BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
            index->nTx = 2;
            BOOST_REQUIRE(block_tree_db.WriteBatchSync({}, 3, {index}));
            const auto insert = [&](const uint256& block_hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
                return restored.InsertBlockIndex(block_hash);
            };
            // Legacy PoS relies on the restored flag before the PoW check.
            BOOST_REQUIRE_EQUAL(block_tree_db.LoadBlockIndexGuts(consensus, insert), test.proof_of_stake);
            if (!test.proof_of_stake) continue;

            const auto* loaded{restored.LookupBlockIndex(hash)};
            BOOST_REQUIRE(loaded);
            BOOST_REQUIRE(loaded->pprev);
            BOOST_CHECK_EQUAL(loaded->pprev->GetBlockHash(), header.hashPrevBlock);
            BOOST_CHECK_EQUAL(loaded->nHeight, index->nHeight);
            BOOST_CHECK_EQUAL(loaded->nFile, index->nFile);
            BOOST_CHECK_EQUAL(loaded->nDataPos, index->nDataPos);
            BOOST_CHECK_EQUAL(loaded->nUndoPos, index->nUndoPos);
            BOOST_CHECK_EQUAL(loaded->nStatus, index->nStatus);
            BOOST_CHECK_EQUAL(loaded->nTx, index->nTx);
            BOOST_CHECK_EQUAL(loaded->nVersion, header.nVersion);
            BOOST_CHECK_EQUAL(loaded->hashMerkleRoot, header.hashMerkleRoot);
            BOOST_CHECK_EQUAL(loaded->nTime, header.nTime);
            BOOST_CHECK_EQUAL(loaded->nBits, header.nBits);
            BOOST_CHECK_EQUAL(loaded->nNonce, header.nNonce);
            BOOST_CHECK_EQUAL(loaded->nFlags, header.nFlags);
            BOOST_CHECK(loaded->IsProofOfStake());
            BOOST_CHECK_EQUAL(loaded->posStakeHash, header.posStakeHash);
            BOOST_CHECK_EQUAL(loaded->posStakeN, header.posStakeN);
            BOOST_CHECK(loaded->posBlockSig == header.posBlockSig);
            const auto restored_header{loaded->GetBlockHeader()};
            BOOST_CHECK_EQUAL(restored_header.GetHash(), hash);
            BOOST_CHECK_EQUAL(restored_header.nFlags, header.nFlags);
            BOOST_CHECK_EQUAL(restored_header.posStakeHash, header.posStakeHash);
            BOOST_CHECK_EQUAL(restored_header.posStakeN, header.posStakeN);
            BOOST_CHECK(restored_header.posBlockSig == header.posBlockSig);
        }
    }
}

BOOST_FIXTURE_TEST_CASE(blockmanager_scan_unlink_already_pruned_files, TestChain100Setup)
{
    // Cap last block file size, and mine new block in a new block file.
    auto& chainman{*Assert(m_node.chainman)};
    auto& blockman{chainman.m_blockman};
    const CBlockIndex* old_tip{WITH_LOCK(chainman.GetMutex(), return chainman.ActiveChain().Tip())};
    WITH_LOCK(chainman.GetMutex(), blockman.GetBlockFileInfo(old_tip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE);
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    // Prune the older block file, but don't unlink it
    int file_number;
    {
        LOCK(chainman.GetMutex());
        file_number = old_tip->GetBlockPos().nFile;
        blockman.PruneOneBlockFile(file_number);
    }

    const FlatFilePos pos(file_number, 0);

    // Check that the file is not unlinked after ScanAndUnlinkAlreadyPrunedFiles
    // if m_have_pruned is not yet set
    WITH_LOCK(chainman.GetMutex(), blockman.ScanAndUnlinkAlreadyPrunedFiles());
    BOOST_CHECK(!CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull());

    // Check that the file is unlinked after ScanAndUnlinkAlreadyPrunedFiles
    // once m_have_pruned is set
    blockman.m_have_pruned = true;
    WITH_LOCK(chainman.GetMutex(), blockman.ScanAndUnlinkAlreadyPrunedFiles());
    BOOST_CHECK(CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull());

    // Check that calling with already pruned files doesn't cause an error
    WITH_LOCK(chainman.GetMutex(), blockman.ScanAndUnlinkAlreadyPrunedFiles());

    // Check that the new tip file has not been removed
    const CBlockIndex* new_tip{WITH_LOCK(chainman.GetMutex(), return chainman.ActiveChain().Tip())};
    BOOST_CHECK_NE(old_tip, new_tip);
    const int new_file_number{WITH_LOCK(chainman.GetMutex(), return new_tip->GetBlockPos().nFile)};
    const FlatFilePos new_pos(new_file_number, 0);
    BOOST_CHECK(!CAutoFile(OpenBlockFile(new_pos, true), SER_DISK, CLIENT_VERSION).IsNull());
}

BOOST_AUTO_TEST_SUITE_END()
