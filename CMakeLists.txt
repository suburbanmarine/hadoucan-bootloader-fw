cmake_minimum_required(VERSION 3.5)

project(stm32h7_qspi_boot C CXX ASM)

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

add_subdirectory(external/stm32cube_fw_h7_v1_4_0)
add_subdirectory(external/tinyxml2)
add_subdirectory(external/mbedtls)

add_subdirectory(external/crc)
add_subdirectory(external/freertos_cpp_util)
add_subdirectory(external/common_util)
add_subdirectory(external/bootloader_util)
add_subdirectory(external/emb_fs_util)
add_subdirectory(external/mbedtls_util)
add_subdirectory(external/tinyxml2_util)
add_subdirectory(external/libusb_dev_cpp)

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

	freertos_v10_heap4
	freertos_v10
	freertos_v10_cmsis

	bootloader_util
	common_util
	freertos_cpp_util

	usb_dev_cpp
	usb_dev_cpp_stm32

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
	COMMAND openocd -f /usr/local/share/openocd/scripts/interface/stlink.cfg -f /usr/local/share/openocd/scripts/target/stm32h7x.cfg -c "program stm32h7_qspi_boot.elf verify reset exit"
	DEPENDS stm32h7_qspi_boot.elf
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM
)

add_custom_target(ram_stm32h7_qspi_boot
	COMMAND openocd -f /usr/local/share/openocd/scripts/interface/stlink.cfg -f /usr/local/share/openocd/scripts/target/stm32h7x.cfg -c "init;reset halt;load_image stm32h7_qspi_boot.elf;verify_image stm32h7_qspi_boot.elf;mem2array estack_reset_handler 32 0x24000000 2;reg msp $estack_reset_handler(0); resume $estack_reset_handler(1);exit"
	DEPENDS stm32h7_qspi_boot.elf
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM
)
