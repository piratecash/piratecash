#!/usr/bin/env bash
# Copyright (c) 2021-2023 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -e -o pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"/../.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-piratecash/piratecashd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}
DOCKER_RELATIVE_PATH=contrib/containers/deploy

BASE_BUILD_DIR=${BASE_BUILD_DIR:-.}

mkdir -p "$DOCKER_RELATIVE_PATH/bin"
rm -f "$DOCKER_RELATIVE_PATH"/bin/*

cp "$BASE_BUILD_DIR"/src/piratecashd    "$DOCKER_RELATIVE_PATH/bin/"
cp "$BASE_BUILD_DIR"/src/piratecash-cli "$DOCKER_RELATIVE_PATH/bin/"
cp "$BASE_BUILD_DIR"/src/piratecash-tx  "$DOCKER_RELATIVE_PATH/bin/"
strip "$DOCKER_RELATIVE_PATH/bin/piratecashd"
strip "$DOCKER_RELATIVE_PATH/bin/piratecash-cli"
strip "$DOCKER_RELATIVE_PATH/bin/piratecash-tx"

docker build --pull -t "$DOCKER_IMAGE":"$DOCKER_TAG" -f "$DOCKER_RELATIVE_PATH/Dockerfile" "$DOCKER_RELATIVE_PATH"
