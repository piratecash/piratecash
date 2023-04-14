#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2018-2023 The PirateCash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Exercise API with -disablewallet.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class DisableWalletTest (BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, [['-disablewallet']])
        self.is_network_split = False
        self.sync_all()

    def run_test (self):
        # Check regression: https://github.com/bitcoin/bitcoin/issues/6963#issuecomment-154548880
        x = self.nodes[0].validateaddress('7TSBtVu959hGEGPKyHjJz9k55RpWrPffXz')
        assert(x['isvalid'] == False)
        x = self.nodes[0].validateaddress('ycwedq2f3sz2Yf9JqZsBCQPxp18WU3Hp4J')
        assert(x['isvalid'] == True)

        # Checking mining to an address without a wallet
        try:
            self.nodes[0].generatetoaddress(1, 'ycwedq2f3sz2Yf9JqZsBCQPxp18WU3Hp4J')
        except JSONRPCException as e:
            assert("Invalid address" not in e.error['message'])
            assert("ProcessNewBlock, block not accepted" not in e.error['message'])
            assert("Couldn't create new block" not in e.error['message'])

        try:
            self.nodes[0].generatetoaddress(1, '7TSBtVu959hGEGPKyHjJz9k55RpWrPffXz')
            raise AssertionError("Must not mine to invalid address!")
        except JSONRPCException as e:
            assert("Invalid address" in e.error['message'])

if __name__ == '__main__':
    DisableWalletTest ().main ()
