#!/usr/bin/env bash

trap 'exit -1' err

set -v

CONTAINER_ID=$(docker create -v $GITHUB_WORKSPACE:/tmp/workspace -it $DOCKER_REPO:${GITHUB_REF##*/}  /bin/bash)
docker start $CONTAINER_ID
docker exec -u $(id -u):$(id -g) -w /tmp/workspace/ $CONTAINER_ID ./generate_cmake.sh
docker exec -u $(id -u):$(id -g) -w /tmp/workspace/ $CONTAINER_ID make -j`nproc` -C build/ram/debug/
docker exec -u $(id -u):$(id -g) -w /tmp/workspace/ $CONTAINER_ID make -j`nproc` -C build/ram/release/
docker exec -u $(id -u):$(id -g) -w /tmp/workspace/ $CONTAINER_ID make -j`nproc` -C build/flash/release/
docker stop $CONTAINER_ID

pushd $GITHUB_WORKSPACE/build/ram/debug/
ls -la
id
sha256sum -b stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin | tee sha256.txt
tar -czf $GITHUB_WORKSPACE/stm32h7_qspi_boot-debug-ram-$GITHUB_SHA.tar.gz    stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin sha256.txt
popd

pushd $GITHUB_WORKSPACE/build/ram/release/
sha256sum -b stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin | tee sha256.txt
tar -czf $GITHUB_WORKSPACE/stm32h7_qspi_boot-release-ram-$GITHUB_SHA.tar.gz  stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin sha256.txt
popd

pushd $GITHUB_WORKSPACE/build/flash/release/
sha256sum -b stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin | tee sha256.txt
tar -czf $GITHUB_WORKSPACE/stm32h7_qspi_boot-release-flash-$GITHUB_SHA.tar.gz  stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin sha256.txt
popd
