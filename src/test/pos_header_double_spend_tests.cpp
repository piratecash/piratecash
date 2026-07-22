// Copyright (c) 2024 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Regression tests for the header-only stake double-spend guard extracted
// from CheckProofOfStake (see HasHeaderOnlyStakeReuse in pos_kernel.cpp).
//
// The guard walks the still-unvalidated proof-of-stake header tail from
// pindex_prev down to the fork point and rejects a rogue fork that reuses the
// same stake UTXO more than once. A previous version of the walk never
// advanced its cursor (it kept re-reading pindex_fork->pprev), so it only ever
// inspected a single node: a reuse two or more blocks deep in the tail slipped
// through, and a fork point at genesis could dereference a null parent. These
// tests pin the corrected behaviour on hand-built CBlockIndex nodes, so they
// exercise the walk directly without the network mining fixture.

#include <chain.h>
#include <pos_kernel.h>
#include <primitives/transaction.h>
#include <sync.h>
#include <uint256.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pos_header_double_spend_tests, BasicTestingSetup)

namespace {
const uint256 STAKE_HASH = uint256S("01");

// Distinct stake outpoints share one tx hash and differ only by output index.
COutPoint Utxo(uint32_t n) { return COutPoint(STAKE_HASH, n); }

// Configure `idx` as a proof-of-stake header node linked to `prev`, carrying
// stake input `stake`. `validated` marks the node as fully script-validated
// (BLOCK_VALID_SCRIPTS), which terminates the walk; otherwise it stays an
// unvalidated header (nStatus == 0).
void LinkStakeNode(CBlockIndex& idx, CBlockIndex* prev, const COutPoint& stake, bool validated)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    idx.pprev = prev;
    idx.nHeight = prev ? prev->nHeight + 1 : 0;
    idx.nFlags |= CBlockIndex::BLOCK_PROOF_OF_STAKE;
    idx.posStakeHash = stake.hash;
    idx.posStakeN = stake.n;
    idx.nStatus = validated ? BLOCK_VALID_SCRIPTS : 0;
}
} // namespace

// The immediate predecessor (pindex_prev itself) reuses the stake UTXO.
BOOST_AUTO_TEST_CASE(reuse_at_immediate_predecessor)
{
    LOCK(::cs_main);
    CBlockIndex fork, n1;
    LinkStakeNode(fork, nullptr, Utxo(9), /*validated=*/true);   // active-chain fork point
    LinkStakeNode(n1,   &fork,   Utxo(1), /*validated=*/false);  // header tail

    BOOST_CHECK(HasHeaderOnlyStakeReuse(&n1, &fork, Utxo(1)));
}

// The stake UTXO is reused two blocks up the tail. The buggy walk that never
// advanced its cursor missed this; the fixed walk finds it. This is the core
// regression for the frozen-cursor bug.
BOOST_AUTO_TEST_CASE(reuse_two_blocks_deep)
{
    LOCK(::cs_main);
    CBlockIndex fork, n1, n2;
    LinkStakeNode(fork, nullptr, Utxo(9), /*validated=*/true);
    LinkStakeNode(n1,   &fork,   Utxo(1), /*validated=*/false);
    LinkStakeNode(n2,   &n1,     Utxo(2), /*validated=*/false);

    // pindex_prev = n2; the reused UTXO (Utxo(1)) lives on n1, one hop deeper.
    BOOST_CHECK(HasHeaderOnlyStakeReuse(&n2, &fork, Utxo(1)));
}

// No node in the tail reuses the queried UTXO -> not flagged.
BOOST_AUTO_TEST_CASE(no_reuse_in_tail)
{
    LOCK(::cs_main);
    CBlockIndex fork, n1, n2;
    LinkStakeNode(fork, nullptr, Utxo(9), /*validated=*/true);
    LinkStakeNode(n1,   &fork,   Utxo(1), /*validated=*/false);
    LinkStakeNode(n2,   &n1,     Utxo(2), /*validated=*/false);

    BOOST_CHECK(!HasHeaderOnlyStakeReuse(&n2, &fork, Utxo(3)));
}

// The walk must stop at the already-validated region and must not flag a UTXO
// carried by a validated ancestor (those blocks are covered by ConnectBlock).
BOOST_AUTO_TEST_CASE(walk_stops_at_validated_ancestor)
{
    LOCK(::cs_main);
    CBlockIndex fork, v, n1;
    LinkStakeNode(fork, nullptr, Utxo(9), /*validated=*/true);
    LinkStakeNode(v,    &fork,   Utxo(1), /*validated=*/true);   // already validated
    LinkStakeNode(n1,   &v,      Utxo(2), /*validated=*/false);  // unvalidated tail

    // Utxo(1) sits in validated block v; the walk stops before inspecting it.
    BOOST_CHECK(!HasHeaderOnlyStakeReuse(&n1, &fork, Utxo(1)));
}

// When the fork point is genesis, the walk can reach a null parent before it
// meets the fork sentinel; the leading null check must prevent a null
// dereference and simply terminate the walk.
BOOST_AUTO_TEST_CASE(genesis_fork_point_null_guard)
{
    LOCK(::cs_main);
    CBlockIndex genesis, n1, unrelated_fork;
    LinkStakeNode(genesis,        nullptr, Utxo(1), /*validated=*/false);
    LinkStakeNode(n1,             &genesis, Utxo(2), /*validated=*/false);
    // Fork sentinel that is NOT on n1's ancestry, so the walk runs off the end
    // of the chain (pprev == nullptr) instead of ever meeting the sentinel.
    LinkStakeNode(unrelated_fork, nullptr, Utxo(7), /*validated=*/true);

    // Must return false without dereferencing a null parent.
    BOOST_CHECK(!HasHeaderOnlyStakeReuse(&n1, &unrelated_fork, Utxo(5)));
}

BOOST_AUTO_TEST_SUITE_END()
