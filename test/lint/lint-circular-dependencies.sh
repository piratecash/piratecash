#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Check for circular dependencies

export LC_ALL=C

EXPECTED_CIRCULAR_DEPENDENCIES=(
    "chainparamsbase -> util/system -> chainparamsbase"
    "index/txindex -> validation -> index/txindex"
    "policy/fees -> txmempool -> policy/fees"
    "policy/policy -> policy/settings -> policy/policy"
    "qt/addresstablemodel -> qt/walletmodel -> qt/addresstablemodel"
    "qt/bantablemodel -> qt/clientmodel -> qt/bantablemodel"
    "qt/bitcoingui -> qt/utilitydialog -> qt/bitcoingui"
    "qt/bitcoingui -> qt/walletframe -> qt/bitcoingui"
    "qt/bitcoingui -> qt/walletview -> qt/bitcoingui"
    "qt/clientmodel -> qt/peertablemodel -> qt/clientmodel"
    "qt/paymentserver -> qt/walletmodel -> qt/paymentserver"
    "qt/recentrequeststablemodel -> qt/walletmodel -> qt/recentrequeststablemodel"
    "qt/transactiontablemodel -> qt/walletmodel -> qt/transactiontablemodel"
    "qt/walletmodel -> qt/walletmodeltransaction -> qt/walletmodel"
    "txmempool -> validation -> txmempool"
    "wallet/fees -> wallet/wallet -> wallet/fees"
    "wallet/wallet -> wallet/walletdb -> wallet/wallet"
    "wallet/coincontrol -> wallet/wallet -> wallet/coincontrol"
    "policy/fees -> policy/policy -> validation -> policy/fees"
    "policy/policy -> validation -> policy/policy"
    "qt/addressbookpage -> qt/bitcoingui -> qt/walletview -> qt/addressbookpage"
    "txmempool -> validation -> validationinterface -> txmempool"
    "wallet/ismine -> wallet/wallet -> wallet/ismine"
    # Dash
    "coinjoin/server -> net_processing -> coinjoin/server"
    "evo/cbtx -> evo/simplifiedmns -> evo/cbtx"
    "evo/deterministicmns -> evo/simplifiedmns -> evo/deterministicmns"
    "evo/deterministicmns -> llmq/commitment -> evo/deterministicmns"
    "evo/deterministicmns -> llmq/utils -> evo/deterministicmns"
    "evo/mnauth -> net_processing -> evo/mnauth"
    "governance/classes -> governance/governance -> governance/classes"
    "governance/governance -> governance/object -> governance/governance"
    "governance/governance -> masternode/sync -> governance/governance"
    "governance/governance -> net_processing -> governance/governance"
    "governance/object -> governance/validators -> governance/object"
    "hdchain -> wallet/walletdb -> hdchain"
    "llmq/quorums -> llmq/utils -> llmq/quorums"
    "llmq/blockprocessor -> net_processing -> llmq/blockprocessor"
    "llmq/chainlocks -> llmq/instantsend -> llmq/chainlocks"
    "llmq/chainlocks -> net_processing -> llmq/chainlocks"
    "llmq/dkgsessionmgr -> net_processing -> llmq/dkgsessionmgr"
    "llmq/instantsend -> net_processing -> llmq/instantsend"
    "llmq/instantsend -> txmempool -> llmq/instantsend"
    "llmq/instantsend -> validation -> llmq/instantsend"
    "llmq/signing -> llmq/signing_shares -> llmq/signing"
    "llmq/signing -> net_processing -> llmq/signing"
    "llmq/signing_shares -> net_processing -> llmq/signing_shares"
    "logging -> util/system -> logging"
    "logging -> util/system -> random -> logging"
    "masternode/payments -> validation -> masternode/payments"
    "net -> netmessagemaker -> net"
    "net_processing -> spork -> net_processing"
    "netaddress -> netbase -> netaddress"
    "qt/appearancewidget -> qt/guiutil -> qt/appearancewidget"
    "qt/bitcoinaddressvalidator -> qt/guiutil -> qt/bitcoinaddressvalidator"
    "qt/bitcoingui -> qt/guiutil -> qt/bitcoingui"
    "qt/guiutil -> qt/optionsdialog -> qt/guiutil"
    "qt/guiutil -> qt/qvalidatedlineedit -> qt/guiutil"
    "core_io -> evo/cbtx -> evo/deterministicmns -> core_io"
    "core_io -> evo/cbtx -> evo/simplifiedmns -> core_io"
    "evo/simplifiedmns -> llmq/blockprocessor -> net_processing -> evo/simplifiedmns"
    "llmq/dkgsession -> llmq/dkgsessionmgr -> llmq/dkgsessionhandler -> llmq/dkgsession"
    "logging -> util/system -> sync -> logging"
    "logging -> util/system -> stacktraces -> logging"
    "logging -> util/system -> util/getuniquepath -> random -> logging"
    "coinjoin/client -> coinjoin/util -> wallet/wallet -> coinjoin/client"
    "qt/appearancewidget -> qt/guiutil -> qt/optionsdialog -> qt/appearancewidget"
    "qt/guiutil -> qt/optionsdialog -> qt/optionsmodel -> qt/guiutil"
    "evo/deterministicmns -> evo/simplifiedmns -> llmq/blockprocessor -> net_processing -> evo/deterministicmns"

    "coinjoin/client -> net_processing -> coinjoin/client"
    "llmq/quorums -> net_processing -> llmq/quorums"
    "llmq/commitment -> llmq/utils -> llmq/commitment"
    "llmq/dkgsession -> llmq/dkgsessionmgr -> llmq/dkgsession"
    "evo/deterministicmns -> validationinterface -> txmempool -> evo/deterministicmns"
    "llmq/chainlocks -> llmq/instantsend -> validation -> llmq/chainlocks"
    "coinjoin/coinjoin -> llmq/chainlocks -> net -> coinjoin/coinjoin"
    "evo/deterministicmns -> llmq/utils -> net -> evo/deterministicmns"
    "policy/fees -> txmempool -> validation -> policy/fees"
    "policy/policy -> policy/settings -> policy/policy"
    "evo/specialtxman -> validation -> evo/specialtxman"
    "bloom -> llmq/commitment -> llmq/utils -> net -> bloom"

    "evo/simplifiedmns -> llmq/blockprocessor -> net_processing -> llmq/snapshot -> evo/simplifiedmns"
    "llmq/blockprocessor -> net_processing -> llmq/snapshot -> llmq/blockprocessor"
    "llmq/dkgsession -> llmq/dkgsessionmgr -> llmq/quorums -> llmq/dkgsession"
    "llmq/dkgsessionmgr -> llmq/quorums -> llmq/dkgsessionmgr"
    "llmq/snapshot -> llmq/utils -> llmq/snapshot"
)

EXIT_CODE=0

CIRCULAR_DEPENDENCIES=()

IFS=$'\n'
for CIRC in $(cd src && ../contrib/devtools/circular-dependencies.py {*,*/*,*/*/*}.{h,cpp} | sed -e 's/^Circular dependency: //'); do
    CIRCULAR_DEPENDENCIES+=( "$CIRC" )
    IS_EXPECTED_CIRC=0
    for EXPECTED_CIRC in "${EXPECTED_CIRCULAR_DEPENDENCIES[@]}"; do
        if [[ "${CIRC}" == "${EXPECTED_CIRC}" ]]; then
            IS_EXPECTED_CIRC=1
            break
        fi
    done
    if [[ ${IS_EXPECTED_CIRC} == 0 ]]; then
        echo "A new circular dependency in the form of \"${CIRC}\" appears to have been introduced."
        echo
        EXIT_CODE=1
    fi
done

for EXPECTED_CIRC in "${EXPECTED_CIRCULAR_DEPENDENCIES[@]}"; do
    IS_PRESENT_EXPECTED_CIRC=0
    for CIRC in "${CIRCULAR_DEPENDENCIES[@]}"; do
        if [[ "${CIRC}" == "${EXPECTED_CIRC}" ]]; then
            IS_PRESENT_EXPECTED_CIRC=1
            break
        fi
    done
    if [[ ${IS_PRESENT_EXPECTED_CIRC} == 0 ]]; then
        echo "Good job! The circular dependency \"${EXPECTED_CIRC}\" is no longer present."
        echo "Please remove it from EXPECTED_CIRCULAR_DEPENDENCIES in $0"
        echo "to make sure this circular dependency is not accidentally reintroduced."
        echo
        EXIT_CODE=1
    fi
done

exit ${EXIT_CODE}
