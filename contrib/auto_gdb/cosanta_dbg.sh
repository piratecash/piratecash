#!/usr/bin/env bash
# use testnet settings,  if you need mainnet,  use ~/.piratecashcore/piratecashd.pid file instead
export LC_ALL=C

piratecash_pid=$(<~/.piratecashcore/testnet3/piratecashd.pid)
sudo gdb -batch -ex "source debug.gdb" piratecashd ${piratecash_pid}
