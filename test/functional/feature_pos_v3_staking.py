#!/usr/bin/env python3
# Copyright (c) 2026 The PirateCash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Proof-of-stake v3 with the real staking thread on two nodes.

Both nodes stake their own coins at the same time after the activation height.
The kernel target saturates on regtest, so every slot is won by both nodes and
same-slot siblings are normal; each node keeps the sibling it saw first. The
clock is mocked and moved one slot at a time while the threads run.
"""

import time

from test_framework.messages import CBlockHeader, from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, force_finish_mnsync

LAST_POW_BLOCK = 300
STAKE_MIN_AGE = 600
POSV3_HEIGHT = 320
SLOT = 10
BLOCKS_BEFORE_RESTART = 15
BLOCKS_AFTER_RESTART = 15
# The staking thread sleeps one second between passes.
STEP_WALL_SECONDS = 1.2
V3_REJECTS = ["bad-nonce-v3", "time-not-slot", "time-not-after-parent", "time-too-new"]


class PosV3StakingTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        common_args = [
            f"-posparams={LAST_POW_BLOCK}:{LAST_POW_BLOCK + 1}:{STAKE_MIN_AGE}",
            f"-testactivationheight=posv3@{POSV3_HEIGHT}",
            "-stakeautocombine=0",
        ]
        self.extra_args = [common_args + ["-staking=0"]] * self.num_nodes
        self.staking_args = common_args + ["-staking=1"]

    def add_options(self, parser):
        # Staking needs a legacy wallet.
        self.add_wallet_options(parser, descriptors=False)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_bdb()

    def log_size(self, node):
        return node.debug_log_path.stat().st_size

    def log_since(self, node, offset):
        with open(node.debug_log_path, "rb") as log:
            log.seek(offset)
            return log.read().decode("utf-8", errors="replace")

    def start_staking(self, i):
        """Restart node i with the staking thread on and get it ready to stake."""
        node = self.nodes[i]
        offset = self.log_size(node)
        node.mocktime = self.mocktime
        # The wallet was created with load_on_startup, so it is loaded when the staking thread starts.
        self.restart_node(i, extra_args=self.staking_args)
        for j in range(self.num_nodes):
            if j != i and self.nodes[j].running:
                self.connect_nodes(i, j)
        # The thread stakes only once the masternode sync has finished and a peer is connected.
        force_finish_mnsync(node)
        self.wait_until(lambda: "PoSMiner started" in self.log_since(node, offset), timeout=10)
        assert "Staking start skipped" not in self.log_since(node, offset)
        return offset

    def minter(self, block_hash):
        """The node whose wallet staked a block: the coinbase pays its first output to the staker."""
        output = self.nodes[0].getblock(block_hash, 2)["tx"][0]["vout"][0]["scriptPubKey"]
        address = output["address"] if "address" in output else output["addresses"][0]
        owners = [i for i, node in enumerate(self.nodes) if node.getaddressinfo(address)["ismine"]]
        assert_equal(len(owners), 1)
        return owners[0]

    def run_until(self, height, done=lambda: True):
        """Move the clock one slot at a time while the threads stake, until the height is reached."""
        start = time.time()
        steps = 0
        while not (min(node.getblockcount() for node in self.nodes) >= height and done()):
            assert time.time() - start < 120, "the staking threads stopped making blocks"
            self.bump_mocktime(SLOT)
            time.sleep(STEP_WALL_SECONDS)
            steps += 1
        self.log.info(f"Height {height} after {steps} slots ({time.time() - start:.1f} s)")

    def check_v3_header_rules(self, node, first, last):
        parent_time = from_hex(CBlockHeader(), node.getblockheader(node.getblockhash(first - 1), False)).nTime
        for height in range(first, last + 1):
            header = from_hex(CBlockHeader(), node.getblockheader(node.getblockhash(height), False))
            assert header.is_pos_v2()
            assert_equal(header.nTime % SLOT, 0)
            assert_greater_than(header.nTime, parent_time)
            assert_equal(header.nNonce, 0)
            parent_time = header.nTime

    def run_test(self):
        self.log.info("Fund both wallets and cross the activation height with generatestake")
        wallets = [node.get_wallet_rpc(self.default_wallet_name) for node in self.nodes]
        addresses = [wallet.getnewaddress() for wallet in wallets]
        for chunk in range(LAST_POW_BLOCK // 10):
            self.generatetoaddress(self.nodes[0], 10, addresses[chunk % 2])
        self.bump_mocktime(STAKE_MIN_AGE * 2)
        for _ in range(POSV3_HEIGHT - LAST_POW_BLOCK):
            self.bump_mocktime(60)
            wallets[0].generatestake(1)
        self.sync_blocks()
        assert_equal(self.nodes[1].getblockcount(), POSV3_HEIGHT)

        self.log.info("Start the staking threads on both nodes")
        log_offsets = [self.start_staking(i) for i in range(self.num_nodes)]

        self.log.info("Both threads stake v3 blocks")
        self.run_until(POSV3_HEIGHT + BLOCKS_BEFORE_RESTART)

        self.log.info("Restart one node; its thread resumes staking")
        restart_height = self.nodes[1].getblockcount()
        restarted_offset = self.start_staking(1)
        self.run_until(restart_height + BLOCKS_AFTER_RESTART,
                       lambda: "PoSMiner : block is submitted" in self.log_since(self.nodes[1], restarted_offset))

        self.log.info("Pause one staker so that a same-height split resolves, then check convergence")
        wallets = [node.get_wallet_rpc(self.default_wallet_name) for node in self.nodes]
        wallets[1].reservebalance(True, 21000000)
        target = max(node.getblockcount() for node in self.nodes) + 1
        self.run_until(target)
        self.sync_blocks(timeout=30)
        wallets[1].reservebalance(False)

        tip = self.nodes[0].getblockcount()
        for node in self.nodes:
            self.check_v3_header_rules(node, POSV3_HEIGHT, tip)
        for node in self.nodes:
            siblings = [t for t in node.getchaintips() if t["status"] != "active"]
            self.log.info(f"node {node.index}: {len(siblings)} side branches, longest {max([t['branchlen'] for t in siblings], default=0)}")
            for sibling in siblings:
                header = from_hex(CBlockHeader(), node.getblockheader(sibling["hash"], False))
                assert_equal(header.nTime % SLOT, 0)
                assert_equal(header.nNonce, 0)
        minters = [self.minter(self.nodes[0].getblockhash(h)) for h in range(POSV3_HEIGHT + 1, tip + 1)]
        self.log.info(f"Blocks {POSV3_HEIGHT + 1}..{tip} staked by node 0: {minters.count(0)}, node 1: {minters.count(1)}")
        assert minters.count(0) > 0 and minters.count(1) > 0

        self.log.info("Neither thread had one of its own blocks refused")
        for node, offset in zip(self.nodes, log_offsets):
            log = self.log_since(node, offset)
            assert "PoSMiner : block is rejected" not in log
            assert "proof check failed" not in log
            for reason in V3_REJECTS:
                # Allowed only for a peer's block that arrived just before this node's clock moved.
                count = log.count(reason)
                if count:
                    self.log.info(f"node {node.index}: {count} x {reason}")
                    assert_equal(reason, "time-too-new")


if __name__ == '__main__':
    PosV3StakingTest().main()
