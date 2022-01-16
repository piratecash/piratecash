// Copyright (c) 2018-2022 The PirateCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net.h"
#include "masternodeconfig.h"
#include "util.h"
#include <base58.h>

CMasternodeConfig masternodeConfig;


void CMasternodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string donationAddress, std::string rewardPercent) {
    CMasternodeEntry cme(alias, ip, privKey, txHash, outputIndex, donationAddress, rewardPercent);
    entries.push_back(cme);
}

bool CMasternodeConfig::read(boost::filesystem::path path) {
    boost::filesystem::ifstream streamConfig(GetMasternodeConfigFile());
    if (!streamConfig.good()) {
        return true; // No masternode.conf file is OK
    }

    for(std::string line; std::getline(streamConfig, line); )
    {
        if(line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        std::string alias, ip, privKey, txHash, outputIndex, reward, donationAddress, rewardPercent;
        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex >> reward)) {
            donationAddress = "";
            rewardPercent = "";
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                LogPrintf("Could not parse masternode.conf line: %s\n", line.c_str());
                streamConfig.close();
                return false;
            }
        } else {
            size_t pos = reward.find_first_of(":");
            if(pos == string::npos) { // no ":" found
                rewardPercent = "100";
                donationAddress = reward;
            } else {
                rewardPercent = reward.substr(pos + 1);
                donationAddress = reward.substr(0, pos);
            }
            CBitcoinAddress address(donationAddress);
            if (!address.IsValid()) {
                LogPrintf("Invalid PIRATE address in masternode.conf line: %s\n", line.c_str());
                streamConfig.close();
                return false;
            }
        }

        add(alias, ip, privKey, txHash, outputIndex, donationAddress, rewardPercent);
    }

    streamConfig.close();
    return true;
}
