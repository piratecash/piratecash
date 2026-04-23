// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/piratecashconsensus.h>
#include <script/interpreter.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>

#include <cstdint>
#include <string>
#include <vector>

FUZZ_TARGET(script_bitcoin_consensus)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    const std::vector<uint8_t> random_bytes_1 = ConsumeRandomLengthByteVector(fuzzed_data_provider);
    const std::vector<uint8_t> random_bytes_2 = ConsumeRandomLengthByteVector(fuzzed_data_provider);
    const CAmount money = ConsumeMoney(fuzzed_data_provider);
    pirateconsensus_error err;
    pirateconsensus_error* err_p = fuzzed_data_provider.ConsumeBool() ? &err : nullptr;
    const unsigned int n_in = fuzzed_data_provider.ConsumeIntegral<unsigned int>();
    const unsigned int flags = fuzzed_data_provider.ConsumeIntegral<unsigned int>();
    assert(pirateconsensus_version() == BITCOINCONSENSUS_API_VER);
    (void)pirateconsensus_verify_script(random_bytes_1.data(), random_bytes_1.size(), random_bytes_2.data(), random_bytes_2.size(), n_in, flags, err_p);
}
