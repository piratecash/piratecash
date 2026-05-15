// Copyright (c) 2025 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>

#include <crypto/scrypt.h>
#include <uint256.h>

#include <array>

namespace {
static constexpr size_t BLOCK_HEADER_SIZE{80};

void Pow_Scrypt(benchmark::Bench& bench)
{
    uint256 hash{};
    std::array<char, BLOCK_HEADER_SIZE> in{};
    bench.minEpochIterations(20).batch(in.size()).unit("byte").run([&] {
        scrypt_1024_1_1_256(in.data(), reinterpret_cast<char*>(hash.begin()));
    });
}
} // anonymous namespace

static void Pow_Scrypt_0080b(benchmark::Bench& bench) { return Pow_Scrypt(bench); }

BENCHMARK(Pow_Scrypt_0080b);
