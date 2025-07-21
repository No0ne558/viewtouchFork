#!/bin/bash
set -e
set -x

uname -a

cmake -H. -B_build_${TOOLCHAIN} -DCMAKE_TOOLCHAIN_FILE="${PWD}/ci/toolchains/${TOOLCHAIN}.cmake" -DCMAKE_INSTALL_PREFIX="${PWD}/_install_${TOOLCHAIN}"
cmake --build _build_${TOOLCHAIN}
cmake --build _build_${TOOLCHAIN} --target install

if [ "$RUN_TESTS" = true ]; then
	CTEST_OUTPUT_ON_FAILURE=1 cmake --build _build_${TOOLCHAIN} --target test
fi


