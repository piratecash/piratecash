#!/usr/bin/env bash
# Copyright (c) 2018-2023 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
# use testnet settings,  if you need mainnet,  use ~/.cosantacore/cosantad.pid file instead
export LC_ALL=C

cosanta_pid=$(<~/.cosantacore/testnet3/cosantad.pid)
sudo gdb -batch -ex "source debug.gdb" cosantad ${cosanta_pid}
