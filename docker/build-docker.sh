#!/usr/bin/env bash

export LC_ALL=C

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-piratecash/piratecashd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/piratecashd docker/bin/
cp $BUILD_DIR/src/piratecash-cli docker/bin/
cp $BUILD_DIR/src/piratecash-tx docker/bin/
strip docker/bin/piratecashd
strip docker/bin/piratecash-cli
strip docker/bin/piratecash-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
