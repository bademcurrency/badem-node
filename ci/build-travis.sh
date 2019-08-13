#!/bin/bash

qt_dir=${1}
src_dir=${2}

set -o errexit
set -o nounset
set -o xtrace
OS=`uname`

mkdir build
pushd build

if [[ ${RELEASE-0} -eq 1 ]]; then
    BUILD_TYPE="RelWithDebInfo"
else
    BUILD_TYPE="Debug"
fi

if [[ ${ASAN_INT-0} -eq 1 ]]; then
    SANITIZERS="-DBADEM_ASAN_INT=ON"
elif [[ ${ASAN-0} -eq 1 ]]; then
    SANITIZERS="-DBADEM_ASAN=ON"
elif [[ ${TSAN-0} -eq 1 ]]; then
    SANITIZERS="-DBADEM_TSAN=ON"
else
    SANITIZERS=""
fi

cmake \
    -G'Unix Makefiles' \
    -DACTIVE_NETWORK=badem_test_network \
    -DBADEM_TEST=ON \
    -DBADEM_GUI=ON \
    -DBADEM_ROCKSDB=ON \
    -DBADEM_WARN_TO_ERR=ON \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DBOOST_ROOT=/usr/local \
    -DQt5_DIR=${qt_dir} \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    ${SANITIZERS} \
    ..


if [[ "$OS" == 'Linux' ]]; then
    cmake --build ${PWD} -- -j2
else
    sudo cmake --build ${PWD} -- -j2
fi

popd

./ci/test.sh ./build
