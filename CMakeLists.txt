cmake_minimum_required(VERSION 3.5)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/cmake-modules/")
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)

project(stm32h7_qspi_boot C CXX ASM)

set(STM32CUBEH7_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/STM32CubeH7)

add_definitions(
	-DSTM32H750xx
	-D__weak=__attribute__\(\(weak\)\)
	-D__packed=__attribute__\(\(__packed__\)\)

	-DUSE_HAL_DRIVER
	-DHSE_VALUE=\(\(uint32_t\)24000000\)

	-DWORD_SIZE=4

	-DSPIFFS_USE_MAGIC
	-DSPIFFS_USE_MAGIC_LENGTH
	-DSPIFFS_LOCK=spiffs_lock_dispatch
	-DSPIFFS_UNLOCK=spiffs_unlock_dispatch
	-DSPIFFS_HAL_CALLBACK_EXTRA
	-DSPIFFS_ALIGNED_OBJECT_INDEX_TABLES=1
	
	-DMBEDTLS_CONFIG_FILE=<config-no-entropy.h>
)

include_directories(
	include
	external/
	external/mbedtls/include
)

add_library(tinyusb_config INTERFACE)
target_include_directories(tinyusb_config
	INTERFACE
		hadoucan-fw/include
)
target_compile_definitions(tinyusb_config
	INTERFACE
		CFG_TUSB_MCU=OPT_MCU_STM32H7
)
target_link_libraries(tinyusb_config
	INTERFACE
		freertos_v10
)

add_library(tinyusb_dcd
	external/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
	external/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c
)
target_include_directories(tinyusb_dcd
	PUBLIC
		external/tinyusb/src
)
target_link_libraries(tinyusb_dcd
	PUBLIC
		tinyusb
		tinyusb_config
		stm32h750xx_cmsis
)

add_subdirectory(external/crc)
add_subdirectory(external/mbedtls)
add_subdirectory(external/STM32CubeH7_cmake)
add_subdirectory(external/tinyusb/src)
add_subdirectory(external/tinyxml2)

add_subdirectory(external/bootloader_util)
add_subdirectory(external/common_util)
add_subdirectory(external/emb_fs_util)
add_subdirectory(external/freertos_cpp_util)
add_subdirectory(external/mbedtls_util)
add_subdirectory(external/tinyxml2_util)

add_library(spiffs
	external/spiffs/src/spiffs_cache.c
	external/spiffs/src/spiffs_check.c
	external/spiffs/src/spiffs_gc.c
	external/spiffs/src/spiffs_hydrogen.c
	external/spiffs/src/spiffs_nucleus.c
)

target_include_directories(spiffs
	PUBLIC
		external/spiffs/src
)

add_executable(stm32h7_qspi_boot.elf
	src/system_stm32h7xx.c

	src/stm32h7xx_it.c
	src/stm32h7xx_hal_timebase_tim.c
	src/stm32h7xx_hal_msp.c

	src/syscalls.c

	src/bootloader_aes_gcm_key.cpp
	
	src/main.cpp
	src/main_cube.c
	src/global_inst.cpp
	src/hal_inst.c
	src/sw_ver.cpp
	
	src/tasks/USB_tx_buffer_task.cpp
	src/tasks/USB_rx_buffer_task.cpp
	src/tasks/Bootloader_task.cpp
	src/tasks/LED_task.cpp
	src/tasks/Logging_task.cpp

	src/tasks/USB_poll.cpp

	src/uart1_printf.cpp

	src/freertos_stubs.cpp
	src/freertos_new.cpp
	src/newlib_stubs.c

	src/Fastboot.cpp
)

target_compile_definitions(stm32h7_qspi_boot.elf
	PRIVATE
		-DGIT_SHA1="${GIT_SHA1}"
)

if(DEFINED ENV{ENC_BOOT_KEY})
	target_compile_definitions(stm32h7_qspi_boot.elf
		PRIVATE
			-DENC_BOOT_KEY=$ENV{ENC_BOOT_KEY}
	)
endif()

target_include_directories(stm32h7_qspi_boot.elf
	PRIVATE
		include
)

target_link_libraries(stm32h7_qspi_boot.elf

	stm32h7xx_hal
	stm32h7xx_ll
	stm32h750xx_cmsis

	freertos_v10
	freertos_v10_heap4
	freertos_v10_cmsis

	bootloader_util
	common_util
	freertos_cpp_util

	tinyusb

	spi_flash
	spiffs_flash
	spiffs_util

	mbedtls_util
	tinyxml2_util

	crc

	spiffs

	mbedtls
	mbedcrypto

	tinyxml2_static
)

# Generate intel hex image
add_custom_command(
	OUTPUT stm32h7_qspi_boot.hex
	COMMAND ${ATOLLIC_PATH}/arm-atollic-eabi-objcopy -Oihex stm32h7_qspi_boot.elf stm32h7_qspi_boot.hex
	DEPENDS stm32h7_qspi_boot.elf
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM	
)

add_custom_target(stm32h7_qspi_boot_hex
	ALL
	DEPENDS stm32h7_qspi_boot.hex
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Generate raw image
add_custom_command(
	OUTPUT stm32h7_qspi_boot.bin
	COMMAND ${ATOLLIC_PATH}/arm-atollic-eabi-objcopy -S -Obinary stm32h7_qspi_boot.elf stm32h7_qspi_boot.bin
	DEPENDS stm32h7_qspi_boot.elf
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM	
)

add_custom_target(stm32h7_qspi_boot_bin
	ALL
	DEPENDS stm32h7_qspi_boot.bin
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)


# Print the image size
add_custom_target(stm32h7_qspi_boot_size
	ALL
	COMMAND ${ATOLLIC_PATH}/arm-atollic-eabi-size --format=sysv stm32h7_qspi_boot.elf
	DEPENDS stm32h7_qspi_boot.elf
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM
)

add_dependencies(stm32h7_qspi_boot_size
	stm32h7_qspi_boot.elf
)

add_custom_target(flash_stm32h7_qspi_boot
	COMMAND openocd -f /usr/local/share/openocd/scripts/interface/stlink-v2.cfg -f /usr/local/share/openocd/scripts/target/stm32h7x.cfg -c "program stm32h7_qspi_boot.elf verify reset exit"
	DEPENDS stm32h7_qspi_boot.elf
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM
)

add_custom_target(ram_stm32h7_qspi_boot
	COMMAND openocd -f /usr/local/share/openocd/scripts/interface/stlink-v2.cfg -f /usr/local/share/openocd/scripts/target/stm32h7x.cfg -c "init;reset halt;load_image stm32h7_qspi_boot.elf;verify_image stm32h7_qspi_boot.elf;mem2array estack_reset_handler 32 0x24000000 2;reg msp $estack_reset_handler(0); resume $estack_reset_handler(1);exit"
	DEPENDS stm32h7_qspi_boot.elf
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM
)
