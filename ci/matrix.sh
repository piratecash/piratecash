#!/usr/bin/env bash
#
# This script is meant to be sourced into the actual build script. It contains the build matrix and will set all
# necessary environment variables for the request build target

export LC_ALL=C.UTF-8

export BUILD_TARGET=${BUILD_TARGET:-linux64}
export PULL_REQUEST=${PULL_REQUEST:-false}
export JOB_NUMBER=${JOB_NUMBER:-1}

export BUILDER_IMAGE_NAME="cosanta-builder-$BUILD_TARGET-$JOB_NUMBER"

export HOST_SRC_DIR=${HOST_SRC_DIR:-$(pwd)}
export HOST_CACHE_DIR=${HOST_CACHE_DIR:-$(pwd)/ci-cache-$BUILD_TARGET}

export SRC_DIR=${SRC_DIR:-$HOST_SRC_DIR}
export BUILD_DIR=$SRC_DIR
export OUT_DIR=$BUILD_DIR/out

export CACHE_DIR=${CACHE_DIR:-$HOST_CACHE_DIR}
export CCACHE_DIR=$CACHE_DIR/ccache

export DOCKER_RUN_VOLUME_ARGS="-v $HOST_SRC_DIR:$SRC_DIR -v $HOST_CACHE_DIR:$CACHE_DIR"
export DOCKER_RUN_ENV_ARGS="-e SRC_DIR=$SRC_DIR -e CACHE_DIR=$CACHE_DIR -e PULL_REQUEST=$PULL_REQUEST -e COMMIT_RANGE=$COMMIT_RANGE -e JOB_NUMBER=$JOB_NUMBER -e BUILD_TARGET=$BUILD_TARGET"
export DOCKER_RUN_ARGS="$DOCKER_RUN_VOLUME_ARGS $DOCKER_RUN_ENV_ARGS"
export DOCKER_RUN_IN_BUILDER="docker run -t --rm -w $SRC_DIR $DOCKER_RUN_ARGS $BUILDER_IMAGE_NAME"

# Default values for targets
export GOAL="install"
export SDK_URL=${SDK_URL:-https://bitcoincore.org/depends-sources/sdks}
MAKEJOBS="-j$(nproc)"
export MAKEJOBS

export RUN_UNITTESTS=true
export RUN_INTEGRATIONTESTS=true

# Configure sanitizers options
export TSAN_OPTIONS="suppressions=${SRC_DIR}/test/sanitizer_suppressions/tsan"

if [ "$BUILD_TARGET" = "arm-linux" ]; then
  export HOST=arm-linux-gnueabihf
  export CHECK_DOC=1
  # -Wno-psabi is to disable ABI warnings: "note: parameter passing for argument of type ... changed in GCC 7.1"
  # This could be removed once the ABI change warning does not show up by default
  export BITCOIN_CONFIG="--enable-reduce-exports --enable-suppress-external-warnings --enable-werror CXXFLAGS=-Wno-psabi"
  export RUN_UNITTESTS=false
  export RUN_INTEGRATIONTESTS=false
elif [ "$BUILD_TARGET" = "win64" ]; then
  export HOST=x86_64-w64-mingw32
  export DPKG_ADD_ARCH="i386"
  export BITCOIN_CONFIG="--enable-gui --enable-reduce-exports --disable-miner"
  export DIRECT_WINE_EXEC_TESTS=true
elif [ "$BUILD_TARGET" = "linux32" ]; then
  export HOST=i686-pc-linux-gnu
  export BITCOIN_CONFIG="--enable-zmq --enable-reduce-exports --enable-crash-hooks"
  export USE_SHELL="/bin/cosanta"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "linux32_ubsan" ]; then
  export HOST=i686-pc-linux-gnu
  export BITCOIN_CONFIG="--enable-zmq --enable-reduce-exports --enable-crash-hooks --with-sanitizers=undefined"
  export USE_SHELL="/bin/cosanta"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "linux64" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export DEP_OPTS="NO_UPNP=1 DEBUG=1"
  export BITCOIN_CONFIG="--enable-zmq --enable-reduce-exports --enable-crash-hooks"
  export CPPFLAGS="-DDEBUG_LOCKORDER -DENABLE_DASH_DEBUG -DARENA_DEBUG"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "linux64_tsan" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export DEP_OPTS="NO_UPNP=1 DEBUG=1"
  export BITCOIN_CONFIG="--enable-zmq --enable-reduce-exports --enable-crash-hooks --with-sanitizers=thread"
  export CPPFLAGS="-DDEBUG_LOCKORDER -DENABLE_DASH_DEBUG -DARENA_DEBUG"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "linux64_fuzz" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export DEP_OPTS="NO_UPNP=1 DEBUG=1"
  export BITCOIN_CONFIG="--enable-zmq --disable-ccache --enable-fuzz --with-sanitizers=fuzzer,address,undefined CC=clang CXX=clang++"
  export CPPFLAGS="-DDEBUG_LOCKORDER -DENABLE_DASH_DEBUG -DARENA_DEBUG"
  export PYZMQ=true
  export RUN_UNITTESTS=false
  export RUN_INTEGRATIONTESTS=false
elif [ "$BUILD_TARGET" = "linux64_cxx20" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export DEP_OPTS="NO_UPNP=1 DEBUG=1"
  export BITCOIN_CONFIG="--enable-zmq --enable-reduce-exports --enable-crash-hooks --enable-c++20 --enable-suppress-external-warnings --enable-werror"
  export CPPFLAGS="-DDEBUG_LOCKORDER -DENABLE_DASH_DEBUG -DARENA_DEBUG"
  export PYZMQ=true
  export RUN_INTEGRATIONTESTS=false
elif [ "$BUILD_TARGET" = "linux64_nowallet" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export DEP_OPTS="NO_WALLET=1"
  export BITCOIN_CONFIG="--enable-reduce-exports"
elif [ "$BUILD_TARGET" = "linux64_release" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export DEP_OPTS="NO_UPNP=1"
  export BITCOIN_CONFIG="--enable-zmq --enable-glibc-back-compat --enable-reduce-exports LDFLAGS=-static-libstdc++"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "mac" ]; then
  export HOST=x86_64-apple-darwin19
  export BITCOIN_CONFIG="--enable-gui --enable-reduce-exports --disable-miner --enable-werror"
  export XCODE_VERSION=11.3.1
  export XCODE_BUILD_ID=11C505
  export GOAL="all deploy"
  export RUN_UNITTESTS=false
  export RUN_INTEGRATIONTESTS=false
fi
