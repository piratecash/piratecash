// Copyright (c) 2019-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/data.h>

namespace benchmark {
namespace data {

#include <bench/data/block813851.raw.h>
// Synthetic fixture based on DASH mainnet block 813851: preserve its 999-transaction
// workload, with nTime=0 for version 1 transactions, a recomputed Merkle root and
// an empty legacy block signature.
// The original header time is unchanged; nBits=0x1f00ffff and nonce=7141 provide valid
// PirateCash scrypt PoW. It is for context-free checks, not connection to a chain.
const std::vector<uint8_t> block813851{std::begin(raw_bench::block813851_raw), std::end(raw_bench::block813851_raw)};

} // namespace data
} // namespace benchmark
