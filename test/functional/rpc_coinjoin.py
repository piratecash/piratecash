#!/usr/bin/env python3
# Copyright (c) 2019-2025 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import random
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import (
    COIN,
    MAX_MONEY,
    uint256_to_string,
)
from test_framework.util import (
    assert_equal,
    assert_is_hex_string,
    assert_raises_rpc_error,
    force_finish_mnsync,
)

# See coinjoin/options.h
COINJOIN_RANDOM_ROUNDS = 3
COINJOIN_ROUNDS_DEFAULT = 4
COINJOIN_ROUNDS_MAX = 16
COINJOIN_ROUNDS_MIN = 2
COINJOIN_TARGET_MAX = int(MAX_MONEY / COIN)
COINJOIN_TARGET_MIN = 2

class CoinJoinTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        # The framework's keypool=1 and -createwalletbackups=0 defaults would
        # trip CheckAutomaticBackup() and stop mixing before the maintenance
        # thread gets to mix (see test_newkeypool_stops_mixing)
        self.extra_args = [['-keypool=200', '-createwalletbackups=10']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args)
        self.start_nodes()

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(wallet_name='w1', blank=True, disable_private_keys=False)
        w1 = node.get_wallet_rpc('w1')
        self.test_salt_presence(w1)
        self.test_coinjoin_start_stop(w1)
        self.test_setcoinjoinamount(w1)
        self.test_setcoinjoinrounds(w1)
        self.test_coinjoinsalt(w1)
        w1.unloadwallet()

        node.createwallet(wallet_name='w3')
        w3 = node.get_wallet_rpc('w3')
        self.generatetoaddress(node, COINBASE_MATURITY + 1, w3.getnewaddress())
        self.test_use_cj_option(w3)
        self.test_use_cj_success(w3)
        w3.unloadwallet()

        if not self.options.descriptors:
            node.createwallet(wallet_name='w_keypool', blank=False, disable_private_keys=False)
            w_keypool = node.get_wallet_rpc('w_keypool')
            # Leave IBD and advance the tip so WaitForAnotherBlock() lets the
            # mixing maintenance thread proceed
            self.generate(self.nodes[0], 2)
            force_finish_mnsync(self.nodes[0])
            self.test_newkeypool_stops_mixing(w_keypool)
            w_keypool.unloadwallet()
        else:
            self.log.info('Skip "newkeypool" mixing test, command is incompatible with descriptor wallets')

        node.createwallet(wallet_name='w2', blank=True, disable_private_keys=True)
        w2 = node.get_wallet_rpc('w2')
        self.test_coinjoinsalt_disabled(w2)
        w2.unloadwallet()

    def test_salt_presence(self, node):
        self.log.info('Salt should be automatically generated in new wallet')
        # Will raise exception if no salt generated
        assert_is_hex_string(node.coinjoinsalt('get'))

    def test_coinjoin_start_stop(self, node):
        self.log.info('"coinjoin" subcommands should update mixing status')
        # Start mix session and ensure it's reported
        node.coinjoin('start')
        cj_info = node.getcoinjoininfo()
        assert_equal(cj_info['enabled'], True)
        assert_equal(cj_info['running'], True)
        # No session has completed, so nothing is waiting to be observed
        assert_equal(cj_info['pending_inputs'], 0)
        # Repeated start should yield error
        assert_raises_rpc_error(-32603, 'Mixing has been started already.', node.coinjoin, 'start')
        # Requesting status shouldn't complain
        node.coinjoin('status')

        # Stop mix session and ensure it's reported
        node.coinjoin('stop')
        cj_info = node.getcoinjoininfo()
        assert_equal(cj_info['enabled'], True)
        assert_equal(cj_info['running'], False)
        # Repeated stop should yield error
        assert_raises_rpc_error(-32603, 'No mix session to stop', node.coinjoin, 'stop')
        # Requesting status should tell us off
        assert_raises_rpc_error(-32603, 'No ongoing mix session', node.coinjoin, 'status')

        # Reset mix session
        assert_equal(node.coinjoin('reset'), "Mixing was reset")

    def test_newkeypool_stops_mixing(self, node):
        self.log.info('"newkeypool" should stop mixing')
        # Wait until the scheduler thread runs a mixing attempt for the wallet:
        # "Not enough funds to mix." is logged while it holds cs_wallet. Before
        # the wallet manager lock-order fix that happened under
        # cs_wallet_manager_map, so the newkeypool call below, which acquires
        # cs_wallet_manager_map while holding cs_wallet, would abort
        # -DDEBUG_LOCKORDER builds with a potential-deadlock error.
        with self.nodes[0].busy_wait_for_debug_log([b'Not enough funds to mix.']):
            node.coinjoin('start')
            assert_equal(node.getcoinjoininfo()['running'], True)
        node.newkeypool()
        assert_equal(node.getcoinjoininfo()['running'], False)

    def simulate_mixing(self, node, inputs, denom, rounds):
        # A same-denomination, fee-free self-spend advances each output by one
        # round. Zero-fee transactions are not relayed, so mine them directly.
        for _ in range(rounds):
            outputs = [{node.getnewaddress(): denom} for _ in inputs]
            raw_tx = node.createrawtransaction(inputs, outputs)
            signed_tx = node.signrawtransactionwithwallet(raw_tx)
            assert_equal(signed_tx['complete'], True)
            txid = node.decoderawtransaction(signed_tx['hex'])['txid']
            self.generateblock(self.nodes[0], output=node.getnewaddress(), transactions=[signed_tx['hex']])
            inputs = [{'txid': txid, 'vout': n} for n in range(len(inputs))]
        return inputs

    def test_use_cj_option(self, node):
        self.log.info('"use_cj" option should spend fully mixed coins only')
        addr = node.getnewaddress()
        utxo = node.listunspent()[0]
        input_ref = {'txid': utxo['txid'], 'vout': utxo['vout']}

        # Automatic coin selection should find no fully mixed coins in this wallet
        assert_raises_rpc_error(-4, 'Unable to locate enough mixed funds for this transaction.',
                                node.send, outputs={addr: 1}, options={'use_cj': True})
        assert_raises_rpc_error(-6, 'Total value of UTXO pool too low to pay for transaction.',
                                node.sendall, recipients=[addr], options={'use_cj': True})

        # Preset non-mixed inputs should be rejected instead of silently spent
        not_mixed_error = f"Input not available. UTXO ({utxo['txid']}:{utxo['vout']}) is not fully mixed."
        raw_tx = node.createrawtransaction([input_ref], {addr: 1})
        assert_raises_rpc_error(-8, not_mixed_error, node.fundrawtransaction, raw_tx, {'use_cj': True})
        assert_raises_rpc_error(-8, not_mixed_error, node.walletcreatefundedpsbt,
                                [input_ref], {addr: 1}, 0, {'use_cj': True})
        assert_raises_rpc_error(-8, not_mixed_error, node.send,
                                outputs={addr: 1}, options={'inputs': [input_ref], 'use_cj': True})
        assert_raises_rpc_error(-8, not_mixed_error, node.sendall,
                                recipients=[addr], options={'inputs': [input_ref], 'use_cj': True})

        # The same input is spendable once "use_cj" is not requested
        assert_equal(node.sendall(recipients=[addr], options={'inputs': [input_ref]})['complete'], True)

    def test_use_cj_success(self, node):
        self.log.info('"use_cj" option should spend fully mixed coins and mark the transaction as CoinJoin')
        # Node-global setting, restored at the end of this subtest
        node.setcoinjoinrounds(COINJOIN_ROUNDS_MIN)

        # Fund two denominated outputs. The funding transaction pays a fee and
        # has non-denominated change, so both outputs start at 0 rounds.
        denom = Decimal('0.00100001')
        funding_txid = node.send(outputs=[{node.getnewaddress(): denom}, {node.getnewaddress(): denom}])['txid']
        self.generate(self.nodes[0], 1)
        funding_tx = node.gettransaction(txid=funding_txid, verbose=True)['decoded']
        inputs = [{'txid': funding_txid, 'vout': out['n']} for out in funding_tx['vout'] if out['value'] == denom]
        assert_equal(len(inputs), 2)

        # COINJOIN_ROUNDS_MIN + COINJOIN_RANDOM_ROUNDS rounds make IsFullyMixed()
        # deterministic regardless of the wallet's salt.
        rounds = COINJOIN_ROUNDS_MIN + COINJOIN_RANDOM_ROUNDS
        inputs = self.simulate_mixing(node, inputs, denom, rounds)

        # Both coins report full rounds and count towards the anonymized balance
        mixed_utxos = [(u['txid'], u['vout']) for u in node.listunspent()
                       if u['coinjoin_rounds'] == COINJOIN_ROUNDS_MIN + COINJOIN_RANDOM_ROUNDS]
        assert_equal(sorted(mixed_utxos), sorted((i['txid'], i['vout']) for i in inputs))
        assert_equal(node.getbalances()['mine']['coinjoin'], 2 * denom)

        # "send" accepts a fully mixed preset input; the transaction is marked as
        # CoinJoin and pays the remainder as fee instead of creating change
        res = node.send(outputs={node.getnewaddress(): Decimal('0.0009')}, options={'inputs': [inputs[0]], 'use_cj': True})
        assert_equal(res['complete'], True)
        tx = node.gettransaction(txid=res['txid'], verbose=True)
        assert_equal(tx['DS'], '1')
        assert_equal([(txin['txid'], txin['vout']) for txin in tx['decoded']['vin']],
                     [(inputs[0]['txid'], inputs[0]['vout'])])
        assert_equal(len(tx['decoded']['vout']), 1)

        # "sendall" with automatic selection sweeps the remaining fully mixed coin
        res = node.sendall(recipients=[node.getnewaddress()], options={'use_cj': True})
        assert_equal(res['complete'], True)
        tx = node.gettransaction(txid=res['txid'], verbose=True)
        assert_equal(tx['DS'], '1')
        assert_equal([(txin['txid'], txin['vout']) for txin in tx['decoded']['vin']],
                     [(inputs[1]['txid'], inputs[1]['vout'])])

        node.setcoinjoinrounds(COINJOIN_ROUNDS_DEFAULT)

    def test_setcoinjoinamount(self, node):
        self.log.info('"setcoinjoinamount" should update mixing target')
        # Test normal and large values
        for value in [COINJOIN_TARGET_MIN, 50, 1200000, COINJOIN_TARGET_MAX]:
            node.setcoinjoinamount(value)
            assert_equal(node.getcoinjoininfo()['max_amount'], value)
        # Test values below minimum and above maximum
        for value in [COINJOIN_TARGET_MIN - 1, COINJOIN_TARGET_MAX + 1]:
            assert_raises_rpc_error(-8, "Invalid amount of PIRATE as mixing goal amount", node.setcoinjoinamount, value)

    def test_setcoinjoinrounds(self, node):
        self.log.info('"setcoinjoinrounds" should update mixing rounds')
        # Test acceptable values
        for value in [COINJOIN_ROUNDS_MIN, COINJOIN_ROUNDS_DEFAULT, COINJOIN_ROUNDS_MAX]:
            node.setcoinjoinrounds(value)
            assert_equal(node.getcoinjoininfo()['max_rounds'], value)
        # Test values below minimum and above maximum
        for value in [COINJOIN_ROUNDS_MIN - 1, COINJOIN_ROUNDS_MAX + 1]:
            assert_raises_rpc_error(-8, "Invalid number of rounds", node.setcoinjoinrounds, value)

    def test_coinjoinsalt(self, node):
        self.log.info('"coinjoinsalt generate" should fail if salt already present')
        assert_raises_rpc_error(-32600, 'Wallet "w1" already has set CoinJoin salt!', node.coinjoinsalt, 'generate')

        self.log.info('"coinjoinsalt" subcommands should succeed if no balance and not mixing')
        # 'coinjoinsalt generate' should return a new salt if overwrite enabled
        s1 = node.coinjoinsalt('get')
        assert_equal(node.coinjoinsalt('generate', True), True)
        s2 = node.coinjoinsalt('get')
        assert s1 != s2

        # 'coinjoinsalt get' should fetch newly generated value (i.e. new salt should persist)
        node.unloadwallet('w1')
        node.loadwallet('w1')
        node = self.nodes[0].get_wallet_rpc('w1')
        assert_equal(s2, node.coinjoinsalt('get'))

        # 'coinjoinsalt set' should work with random hashes
        s1 = uint256_to_string(random.getrandbits(256))
        node.coinjoinsalt('set', s1)
        assert_equal(s1, node.coinjoinsalt('get'))
        assert s1 != s2

        # 'coinjoinsalt set' shouldn't work with nonsense values
        s2 = format(0, '064x')
        assert_raises_rpc_error(-8, "Invalid CoinJoin salt value", node.coinjoinsalt, 'set', s2, True)
        s2 = s2[0:63] + 'h'
        assert_raises_rpc_error(-8, "salt must be hexadecimal string (not '%s')" % s2, node.coinjoinsalt, 'set', s2, True)

        self.log.info('"coinjoinsalt generate" and "coinjoinsalt set" should fail if mixing')
        # Start mix session
        node.coinjoin('start')
        assert_equal(node.getcoinjoininfo()['running'], True)

        # 'coinjoinsalt generate' and 'coinjoinsalt set' should fail when mixing
        assert_raises_rpc_error(-4, 'Wallet "w1" is currently mixing, cannot change salt!', node.coinjoinsalt, 'generate', True)
        assert_raises_rpc_error(-4, 'Wallet "w1" is currently mixing, cannot change salt!', node.coinjoinsalt, 'set', s1, True)

        # 'coinjoinsalt get' should still work
        assert_equal(node.coinjoinsalt('get'), s1)

        # Stop mix session
        node.coinjoin('stop')
        assert_equal(node.getcoinjoininfo()['running'], False)

        # 'coinjoinsalt generate' and 'coinjoinsalt set' should start working again
        assert_equal(node.coinjoinsalt('generate', True), True)
        assert_equal(node.coinjoinsalt('set', s1, True), True)

    def test_coinjoinsalt_disabled(self, node):
        self.log.info('"coinjoinsalt" subcommands should fail if private keys disabled')
        for subcommand in ['generate', 'get']:
            assert_raises_rpc_error(-32600, 'Wallet "w2" has private keys disabled, cannot perform CoinJoin!', node.coinjoinsalt, subcommand)
        s1 = uint256_to_string(random.getrandbits(256))
        assert_raises_rpc_error(-32600, 'Wallet "w2" has private keys disabled, cannot perform CoinJoin!', node.coinjoinsalt, 'set', s1)

if __name__ == '__main__':
    CoinJoinTest().main()
