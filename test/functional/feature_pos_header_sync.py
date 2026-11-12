#!/usr/bin/env python3
# Copyright (c) 2026 The PirateCash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Header sync of a proof-of-stake chain.

A PoSv2 header can be checked only once its stake is visible from the tip.
Below the last checkpoint headers are taken in whole segments that hash to a
checkpoint; above it they wait until the tip gets close enough.
"""

from test_framework.governance import EXPECTED_STDERR_NO_GOV_PRUNE
from test_framework.messages import CBlockHeader, from_hex, msg_headers
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, p2p_port

LAST_POW_BLOCK = 300
STAKE_MIN_AGE = 600
POS_BLOCKS = 60
CHECKPOINT_HEIGHTS = [250, 320, 350]
PRESYNC_RETRY_INTERVAL = 10 * 60

STAKER, NO_CHECKPOINTS, CHECKPOINTS, PRUNED, WRONG_CHECKPOINT, P2P_TARGET = range(6)


class HeadersPeer(P2PInterface):
    """Answers getheaders from a fixed list of headers."""

    def __init__(self, headers):
        super().__init__()
        self.headers = headers
        for header in headers:
            header.rehash()

    def on_getheaders(self, message):
        known = {header.sha256: i for i, header in enumerate(self.headers)}
        start = 0
        for block_hash in message.locator.vHave:
            if block_hash in known:
                start = known[block_hash] + 1
                break
        self.send_message(msg_headers(self.headers[start:]))

    # The node asks for compressed headers; plain ones are an equally valid answer.
    on_getheaders2 = on_getheaders


class PosHeaderSyncTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6
        self.common_args = [f"-posparams={LAST_POW_BLOCK}:{LAST_POW_BLOCK + 1}:{STAKE_MIN_AGE}", "-stakeautocombine=0", "-staking=0"]
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

    def restart_fresh(self, i, extra_args):
        self.nodes[i].mocktime = self.mocktime
        self.restart_node(i, extra_args=self.common_args + extra_args)

    def connect_to_staker(self, i):
        self.nodes[i].addnode(f"127.0.0.1:{p2p_port(STAKER)}", "onetry")

    def wait_for_tip(self, i, block_hash):
        self.wait_until(lambda: self.nodes[i].getbestblockhash() == block_hash, timeout=120)

    def log_count(self, i, text):
        with open(self.nodes[i].debug_log_path, encoding="utf-8", errors="replace") as log:
            return sum(text in line for line in log)

    def checkpoint_args(self, heights, hashes=None):
        hashes = hashes or {height: self.nodes[STAKER].getblockhash(height) for height in heights}
        return [f"-checkpoint={height}:{hashes[height]}" for height in heights]

    def build_chain(self):
        node = self.nodes[STAKER]
        self.staker = node.get_wallet_rpc(self.default_wallet_name)
        node.createwallet(wallet_name="other", descriptors=False)
        other_address = node.get_wallet_rpc("other").getnewaddress()
        node.unloadwallet("other")

        # The staking wallet offers a coinbase one block before the header rule accepts it,
        # so its coinbases end 130 blocks below the last proof-of-work block.
        self.generatetoaddress(node, LAST_POW_BLOCK - 130, self.staker.getnewaddress(), sync_fun=self.no_op)
        self.generatetoaddress(node, 130, other_address, sync_fun=self.no_op)
        self.bump_mocktime(STAKE_MIN_AGE * 2, nodes=self.running_nodes())
        self.stake(POS_BLOCKS)
        assert_equal(node.getblockcount(), LAST_POW_BLOCK + POS_BLOCKS)

    def test_window_mode_from_genesis(self):
        self.log.info("A node without checkpoints syncs the whole chain waiting for stakes to become visible")
        tip = self.nodes[STAKER].getbestblockhash()
        self.restart_fresh(NO_CHECKPOINTS, [])
        self.connect_to_staker(NO_CHECKPOINTS)
        self.wait_for_tip(NO_CHECKPOINTS, tip)
        assert_greater_than(self.log_count(NO_CHECKPOINTS, "deferred "), 0)
        assert_equal(self.log_count(NO_CHECKPOINTS, "Misbehaving"), 0)

    def test_anchored_segments(self, i, extra_args):
        tip = self.nodes[STAKER].getbestblockhash()
        self.restart_fresh(i, self.checkpoint_args(CHECKPOINT_HEIGHTS) + extra_args)
        self.connect_to_staker(i)
        self.wait_for_tip(i, tip)
        assert_equal(self.log_count(i, "headers up to"), len(CHECKPOINT_HEIGHTS))
        assert_equal(self.log_count(i, "Misbehaving"), 0)

    def test_wrong_checkpoint(self):
        self.log.info("A chain that does not hash to a checkpoint is not accepted at all")
        wrong = {250: self.nodes[STAKER].getblockhash(251)}
        self.restart_fresh(WRONG_CHECKPOINT, self.checkpoint_args([250], wrong))
        with self.nodes[WRONG_CHECKPOINT].assert_debug_log(["headers do not match a checkpoint"], timeout=30):
            self.connect_to_staker(WRONG_CHECKPOINT)
        info = self.nodes[WRONG_CHECKPOINT].getblockchaininfo()
        assert_equal(info["headers"], 0)
        assert_equal(info["blocks"], 0)
        # The peer is punished once, not asked again in a loop.
        assert_equal(self.log_count(WRONG_CHECKPOINT, "headers do not match a checkpoint"), 1)
        self.stop_node(WRONG_CHECKPOINT)

    def test_reorg(self):
        self.log.info("Nodes without a transaction index follow a competing branch")
        followers = [NO_CHECKPOINTS, PRUNED]
        self.stake(1)
        old_tip = self.nodes[STAKER].getbestblockhash()
        for i in followers:
            self.wait_for_tip(i, old_tip)
            self.nodes[i].setnetworkactive(False)
        self.nodes[CHECKPOINTS].setnetworkactive(False)

        self.nodes[STAKER].invalidateblock(old_tip)
        self.stake(2)
        new_tip = self.nodes[STAKER].getbestblockhash()
        assert old_tip != new_tip

        for i in followers:
            self.nodes[i].setnetworkactive(True)
            self.connect_to_staker(i)
            self.wait_for_tip(i, new_tip)
            assert_equal(self.log_count(i, "Misbehaving"), 0)

    def test_catch_up(self):
        self.log.info("A node that was offline for a while catches up")
        self.stake(30)
        self.nodes[CHECKPOINTS].setnetworkactive(True)
        self.connect_to_staker(CHECKPOINTS)
        self.wait_for_tip(CHECKPOINTS, self.nodes[STAKER].getbestblockhash())
        assert_equal(self.log_count(CHECKPOINTS, "Misbehaving"), 0)

    def chain_headers(self):
        node = self.nodes[STAKER]
        return [from_hex(CBlockHeader(), node.getblockheader(node.getblockhash(height), False))
                for height in range(1, self.chain_height + 1)]

    def test_p2p_headers(self):
        node = self.nodes[P2P_TARGET]
        self.restart_fresh(P2P_TARGET, self.checkpoint_args(CHECKPOINT_HEIGHTS, self.checkpoint_hashes))

        self.log.info("A peer whose chain ends below a checkpoint is asked again only after a pause, and dropped after three failures")
        short_chain = "ends below a checkpoint, trying another peer"
        with node.assert_debug_log([short_chain], timeout=30):
            peer = node.add_p2p_connection(HeadersPeer(self.chain_headers()[:100]), wait_for_verack=False)
        for failures in (2, 3):
            assert_equal(self.log_count(P2P_TARGET, short_chain), failures - 1)
            assert peer.is_connected
            self.bump_mocktime(PRESYNC_RETRY_INTERVAL + 1, nodes=[node])
            self.wait_until(lambda: self.log_count(P2P_TARGET, short_chain) == failures)
        peer.wait_for_disconnect()
        assert_equal(node.getblockchaininfo()["headers"], 0)

        self.log.info("A header changed below a checkpoint breaks the match: the peer is dropped, nothing is kept")
        headers = self.chain_headers()
        headers[249].nNonce += 1
        peer = node.add_p2p_connection(HeadersPeer(headers), wait_for_verack=False)
        peer.wait_for_disconnect()
        assert_equal(node.getblockchaininfo()["headers"], 0)

        self.log.info("A PoSv2 signature of the wrong size is refused; the segments before it stay")
        headers = self.chain_headers()
        assert headers[309].is_pos_v2()
        headers[309].posBlockSig = headers[309].posBlockSig[:-1]
        # The signature is outside the hash, so the peer may be an honest relayer: it is neither punished nor dropped.
        with node.assert_debug_log(["are unusable, trying another peer"], timeout=30):
            bad_signature_peer = node.add_p2p_connection(HeadersPeer(headers), wait_for_verack=False)
        assert_equal(node.getblockchaininfo()["headers"], 250)

        self.log.info("Untouched headers from another peer are accepted up to the last checkpoint")
        peer = node.add_p2p_connection(HeadersPeer(self.chain_headers()))
        self.wait_until(lambda: node.getblockchaininfo()["headers"] >= CHECKPOINT_HEIGHTS[-1])
        assert peer.is_connected
        assert bad_signature_peer.is_connected
        assert_equal(node.getblockchaininfo()["blocks"], 0)
        # The peer that failed was not asked again in a loop.
        assert_equal(self.log_count(P2P_TARGET, "are unusable, trying another peer"), 1)
        assert_equal(self.log_count(P2P_TARGET, "Misbehaving"), 1)

    def run_test(self):
        for i in range(1, self.num_nodes):
            self.stop_node(i)
        self.build_chain()
        self.chain_height = self.nodes[STAKER].getblockcount()
        self.checkpoint_hashes = {height: self.nodes[STAKER].getblockhash(height) for height in CHECKPOINT_HEIGHTS}

        self.test_window_mode_from_genesis()
        self.log.info("A node with checkpoints takes the headers below the last one in segments")
        self.test_anchored_segments(CHECKPOINTS, [])
        self.log.info("So does a pruned node")
        self.test_anchored_segments(PRUNED, ["-prune=1000"])
        self.test_wrong_checkpoint()
        self.test_p2p_headers()
        self.test_reorg()
        self.test_catch_up()
        self.stop_node(PRUNED, expected_stderr=EXPECTED_STDERR_NO_GOV_PRUNE)


if __name__ == '__main__':
    PosHeaderSyncTest().main()
