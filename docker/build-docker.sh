#!/usr/bin/env bash

export LC_ALL=C

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-cosanta/cosantad-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/cosantad docker/bin/
cp $BUILD_DIR/src/cosanta-cli docker/bin/
cp $BUILD_DIR/src/cosanta-tx docker/bin/
strip docker/bin/cosantad
strip docker/bin/cosanta-cli
strip docker/bin/cosanta-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
