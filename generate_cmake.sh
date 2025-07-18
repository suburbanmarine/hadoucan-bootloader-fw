#!/usr/bin/env bash

trap 'exit -1' err

if [ -d build ]; then rm -rf build; fi

BASE_PATH=$PWD

RAM_TOOL="-DCMAKE_TOOLCHAIN_FILE=$BASE_PATH/Toolchain_stm32h750_gcc_ram.cmake"
FLASH_TOOL="-DCMAKE_TOOLCHAIN_FILE=$BASE_PATH/Toolchain_stm32h750_gcc_flash.cmake"
FLASH_SEMI_TOOL="-DCMAKE_TOOLCHAIN_FILE=$BASE_PATH/Toolchain_stm32h750_gcc_flash_semihosting.cmake"

TINYXML2_OPTS="-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTS=OFF"

MBEDTLS_OPTS="-DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF"

# mkdir -p build/ram/release
# pushd build/ram/release
# cmake -DCMAKE_BUILD_TYPE=Release $TINYXML2_OPTS $MBEDTLS_OPTS $RAM_TOOL $BASE_PATH
# popd

# mkdir -p build/ram/relwithdebinfo
# pushd build/ram/relwithdebinfo
# cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo $TINYXML2_OPTS $MBEDTLS_OPTS $RAM_TOOL $BASE_PATH
# popd

# mkdir -p build/ram/debug
# pushd build/ram/debug
# cmake -DCMAKE_BUILD_TYPE=Debug $TINYXML2_OPTS $MBEDTLS_OPTS $RAM_TOOL $BASE_PATH
# popd

mkdir -p build/flash/release
pushd build/flash/release
cmake -DCMAKE_BUILD_TYPE=Release $TINYXML2_OPTS $MBEDTLS_OPTS $FLASH_TOOL $BASE_PATH
popd

mkdir -p build/flash/relwithdebinfo
pushd build/flash/relwithdebinfo
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo $TINYXML2_OPTS $MBEDTLS_OPTS $FLASH_TOOL $BASE_PATH
popd

mkdir -p build/flash/debug
pushd build/flash/debug
cmake -DCMAKE_BUILD_TYPE=Debug $TINYXML2_OPTS $MBEDTLS_OPTS $FLASH_TOOL $BASE_PATH
popd

mkdir -p build/flash_semihost/release
pushd build/flash_semihost/release
cmake -DCMAKE_BUILD_TYPE=Release $TINYXML2_OPTS $MBEDTLS_OPTS $FLASH_SEMI_TOOL $BASE_PATH
popd

mkdir -p build/flash_semihost/relwithdebinfo
pushd build/flash_semihost/relwithdebinfo
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo $TINYXML2_OPTS $MBEDTLS_OPTS $FLASH_SEMI_TOOL $BASE_PATH
popd

mkdir -p build/flash_semihost/debug
pushd build/flash_semihost/debug
cmake -DCMAKE_BUILD_TYPE=Debug $TINYXML2_OPTS $MBEDTLS_OPTS $FLASH_SEMI_TOOL $BASE_PATH
popd
