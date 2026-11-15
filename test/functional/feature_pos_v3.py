#!/usr/bin/env python3
# Copyright (c) 2026 The PirateCash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Proof-of-stake v3: slot-aligned block times and a zero nNonce.

From the activation height a PoS block's time is a multiple of the slot, after
its parent's, and at most one slot minus a second ahead of the node's clock,
and its nNonce is zero.

On regtest every kernel meets the target (it saturates) and the difficulty does
not retarget (fPowNoRetargeting), so neither the v3 stake modifier nor the x16
scaling of the difficulty window at the fork can be observed here: unit tests
cover them (pos_staking_tests, pow_tests).
"""

import random

from test_framework.address import base58_to_byte
from test_framework.crypto import secp256k1
from test_framework.key import ORDER
from test_framework.messages import CBlock, CBlockHeader, from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error, p2p_port

LAST_POW_BLOCK = 300
STAKE_MIN_AGE = 600
POSV3_HEIGHT = 320
SLOT = 10
TOLERANCE = SLOT - 1

STAKER, FOLLOWER = range(2)


def sign_block(block, secret, compressed):
    """Sign the block hash the way CKey::SignCompact does."""
    block.rehash()
    z = int.from_bytes(block.sha256.to_bytes(32, "little"), "big")
    while True:
        k = random.randrange(1, ORDER)
        R = k * secp256k1.G
        r = int(R.x) % ORDER
        s = pow(k, -1, ORDER) * (z + secret * r) % ORDER
        if r == 0 or s == 0:
            continue
        recid = (int(R.y) & 1) | (2 if int(R.x) >= ORDER else 0)
        if s > ORDER // 2:
            s = ORDER - s
            recid ^= 1
        block.posBlockSig = bytes([27 + recid + (4 if compressed else 0)]) + r.to_bytes(32, "big") + s.to_bytes(32, "big")
        return


class PosV3Test(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.common_args = [
            f"-posparams={LAST_POW_BLOCK}:{STAKE_MIN_AGE}",
            f"-testactivationheight=posv3@{POSV3_HEIGHT}",
            "-stakeautocombine=0",
            "-staking=0",
        ]
        self.extra_args = [self.common_args] * self.num_nodes

    def add_options(self, parser):
        # Staking needs a legacy wallet.
        self.add_wallet_options(parser, descriptors=False)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_bdb()

    def setup_network(self):
        self.setup_nodes()

    def running_nodes(self):
        return [node for node in self.nodes if node.running]

    def stake(self, count):
        for _ in range(count):
            self.bump_mocktime(60, nodes=self.running_nodes())
            self.staker.generatestake(1)

    def header(self, node, height):
        return from_hex(CBlockHeader(), node.getblockheader(node.getblockhash(height), False))

    def check_v3_chain(self, node):
        """Every v3 block is on a slot after its parent and has a zero nNonce."""
        tip = node.getblockcount()
        assert_greater_than(tip, POSV3_HEIGHT)
        parent_time = self.header(node, POSV3_HEIGHT - 1).nTime
        for height in range(POSV3_HEIGHT, tip + 1):
            header = self.header(node, height)
            assert header.is_pos_v2()
            assert_equal(header.nTime % SLOT, 0)
            assert_greater_than(header.nTime, parent_time)
            assert_equal(header.nNonce, 0)
            parent_time = header.nTime

    def stake_key(self, node, block_hash):
        """The key that signed a staked block: the coinbase pays its first output to it."""
        output = node.getblock(block_hash, 2)["tx"][0]["vout"][0]["scriptPubKey"]
        address = output["address"] if "address" in output else output["addresses"][0]
        payload, _ = base58_to_byte(self.staker.dumpprivkey(address))
        return int.from_bytes(payload[:32], "big"), len(payload) == 33

    def test_stake_across_the_fork(self):
        self.log.info("Stake across the activation height")
        node = self.nodes[STAKER]
        self.staker = node.get_wallet_rpc(self.default_wallet_name)
        self.generatetoaddress(node, LAST_POW_BLOCK - 1, self.staker.getnewaddress(), sync_fun=self.no_op)
        self.log.info("No staking up to the last proof-of-work block")
        assert_raises_rpc_error(-1, f"Cannot stake block {LAST_POW_BLOCK}: proof-of-stake starts at height {LAST_POW_BLOCK + 1}",
                                self.staker.generatestake, 1)
        self.generatetoaddress(node, 1, self.staker.getnewaddress(), sync_fun=self.no_op)
        self.bump_mocktime(STAKE_MIN_AGE * 2, nodes=self.running_nodes())
        self.stake(POSV3_HEIGHT - LAST_POW_BLOCK)
        assert_equal(node.getblockcount(), POSV3_HEIGHT)

        self.log.info("The last pre-fork block carries the v2 modifier in nNonce, the first v3 block zero")
        assert self.header(node, POSV3_HEIGHT - 1).nNonce != 0
        assert_equal(self.header(node, POSV3_HEIGHT).nNonce, 0)

        self.log.info("A one-block reorganization across the activation height")
        first = node.getbestblockhash()
        node.invalidateblock(first)
        assert_equal(node.getblockcount(), POSV3_HEIGHT - 1)
        # If the wallet rebuilds the invalidated block (same coin, same earliest slot), it is refused and
        # that stake is left out, so another coin or slot is used; no coin is locked by hand.
        self.stake(1)
        assert_equal(node.getblockcount(), POSV3_HEIGHT)
        assert node.getbestblockhash() != first

        self.stake(10)
        assert_equal(node.getblockcount(), POSV3_HEIGHT + 10)
        self.check_v3_chain(node)

    def test_invalid_blocks(self):
        node = self.nodes[STAKER]
        tip_hash = node.getbestblockhash()
        tip_hex = node.getblock(tip_hash, 0)
        tip = from_hex(CBlock(), tip_hex)
        parent_time = node.getblockheader(node.getblockheader(tip_hash)["previousblockhash"])["time"]
        secret, compressed = self.stake_key(node, tip_hash)

        def submit(**changes):
            block = from_hex(CBlock(), tip_hex)
            for field, value in changes.items():
                setattr(block, field, value)
            sign_block(block, secret, compressed)
            return node.submitblock(block.serialize().hex())

        self.log.info("A sibling of the tip on another valid slot, signed here, is accepted: the signing works")
        sibling = (self.mocktime + TOLERANCE) // SLOT * SLOT
        if sibling == tip.nTime:
            sibling -= SLOT
        if sibling <= parent_time:
            # The tip took the only slot there is yet.
            self.bump_mocktime(SLOT, nodes=self.running_nodes())
            sibling = (self.mocktime + TOLERANCE) // SLOT * SLOT
        assert parent_time < sibling <= self.mocktime + TOLERANCE and sibling != tip.nTime
        assert submit(nTime=sibling) in (None, "inconclusive")

        self.log.info("Blocks that break the v3 rules are refused with the matching reason")
        assert_equal(submit(nTime=tip.nTime + 1), "time-not-slot")
        assert_equal(submit(nTime=parent_time), "time-not-after-parent")
        too_new = (self.mocktime + TOLERANCE) // SLOT * SLOT + SLOT
        assert_greater_than(too_new, self.mocktime + TOLERANCE)
        assert_equal(submit(nTime=too_new), "time-too-new")
        assert_equal(submit(nNonce=1), "bad-nonce-v3")
        assert_equal(node.getbestblockhash(), tip_hash)

        self.log.info("A block that was too new is accepted once the clock has caught up")
        self.bump_mocktime(SLOT, nodes=self.running_nodes())
        assert too_new <= self.mocktime + TOLERANCE
        assert submit(nTime=too_new) in (None, "inconclusive")
        assert_equal(node.getbestblockhash(), tip_hash)

    def test_restart(self):
        self.log.info("A node restarted after the fork keeps staking v3 blocks")
        node = self.nodes[STAKER]
        node.mocktime = self.mocktime
        self.restart_node(STAKER, extra_args=self.common_args)
        self.staker = node.get_wallet_rpc(self.default_wallet_name)
        self.stake(5)
        self.check_v3_chain(node)

    def test_sync_from_scratch(self):
        self.log.info("A new node syncs the chain through the activation height")
        follower = self.nodes[FOLLOWER]
        follower.mocktime = self.mocktime
        self.start_node(FOLLOWER, extra_args=self.common_args)
        follower.addnode(f"127.0.0.1:{p2p_port(STAKER)}", "onetry")
        self.wait_until(lambda: follower.getbestblockhash() == self.nodes[STAKER].getbestblockhash(), timeout=120)
        self.check_v3_chain(follower)

        self.log.info("and follows new blocks")
        self.stake(3)
        self.wait_until(lambda: follower.getbestblockhash() == self.nodes[STAKER].getbestblockhash(), timeout=60)

    def run_test(self):
        self.stop_node(FOLLOWER)
        self.test_stake_across_the_fork()
        self.test_invalid_blocks()
        self.test_restart()
        self.test_sync_from_scratch()


if __name__ == '__main__':
    PosV3Test().main()
