#!/usr/bin/env bash

trap 'exit -1' err

set -v

CONTAINER_ID=$(docker create -v $GITHUB_WORKSPACE:/tmp/workspace -it $DOCKER_REPO:${GITHUB_REF##*/}  /bin/bash)
docker start $CONTAINER_ID
docker exec -u $(id -u):$(id -g) -w /tmp/workspace/ $CONTAINER_ID ./generate_cmake.sh
docker exec -u $(id -u):$(id -g) -w /tmp/workspace/ $CONTAINER_ID make -j`nproc` -C build/flash/debug/
docker exec -u $(id -u):$(id -g) -w /tmp/workspace/ $CONTAINER_ID make -j`nproc` -C build/flash/release/
docker stop $CONTAINER_ID

pushd $GITHUB_WORKSPACE/build/ram/debug/stm32h7_qspi_boot
ls -la
id
sha256sum -b stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin | tee sha256.txt
tar -czf $GITHUB_WORKSPACE/stm32h7_qspi_boot-debug-$GITHUB_SHA.tar.gz    stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin sha256.txt
popd

pushd $GITHUB_WORKSPACE/build/ram/release/stm32h7_qspi_boot
sha256sum -b stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin | tee sha256.txt
tar -czf $GITHUB_WORKSPACE/stm32h7_qspi_boot-release-$GITHUB_SHA.tar.gz  stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex stm32h7_qspi_boot.bin sha256.txt
popd
