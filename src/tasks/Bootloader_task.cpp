#include "tasks/Bootloader_task.hpp"

#include "bootloader_aes_gcm_key.hpp"

#include "freertos_cpp_util/logging/Global_logger.hpp"

#include "sw_ver.hpp"

#include "hal_inst.h"
#include "stm32h7xx_hal.h"

#include "bootloader_util/Intel_hex_loader.hpp"
#include "common_util/Byte_util.hpp"

#include "mbedtls_util/AES_GCM_aux_data.hpp"
#include "mbedtls_util/mbed_aes128_gcm_dec.hpp"

#include "freertos_cpp_util/logging/Global_logger.hpp"

#include "global_inst.hpp"

#include <tusb.h>

#include <cctype>
#include <cinttypes>
#include <functional>

using freertos_util::logging::LOG_LEVEL;

uint32_t Bootloader_task::handle_tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state)
{
	uint32_t bwPollTimeout_ms = 0;

	switch(state)
	{
		case DFU_DNBUSY:
		{
			bwPollTimeout_ms = 1;
			break;
		}
		case DFU_MANIFEST:
		{
			bwPollTimeout_ms = 10;
			break;
		}
		default:
		{
			bwPollTimeout_ms = 0;
			break;
		}
	}

	return bwPollTimeout_ms;
}
void Bootloader_task::handle_tud_dfu_download_cb(uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	if(alt != 0)
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_FILE);
		return;
	}

	if(block_num == 0)
	{
		if( m_fd )
		{
			m_fd.reset();
		}

		m_fd = std::make_shared<LFS_file>(&m_fs);
		if(m_fd->open("app.bin.tmp", LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR) < 0)
		{
			m_fd.reset();
			tud_dfu_finish_flashing(DFU_STATUS_ERR_FILE);
			return;
		}

		mbedtls_md5_init(&m_fd_md5_ctx);
		if(mbedtls_md5_starts_ret(&m_fd_md5_ctx) != 0)
		{
			m_fd.reset();
			tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
		}
	}

	if( ! m_fd )
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_FILE);
		return;
	}

	// copy in
	const size_t offset = size_t(block_num) * m_download_block_size;

	if((offset + length) > m_mem_size)
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
		return;
	}

	memcpy(m_mem_base + offset, data, length);

	if(mbedtls_md5_update_ret(&m_fd_md5_ctx, data, length) != 0)
	{
		m_fd.reset();
		tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
		return;
	}

	// Commit to flash
	lfs_ssize_t ret = lfs_file_write(m_fs.get_fs(), m_fd->get_fd(), data, length);
	if((ret < 0) || (ret != length))
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
		return;
	}

	tud_dfu_finish_flashing(DFU_STATUS_OK);
}
void Bootloader_task::handle_tud_dfu_manifest_cb(uint8_t alt)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	if(alt != 0)
	{
		m_fd.reset();
		tud_dfu_finish_flashing(DFU_STATUS_ERR_FILE);
		return;
	}

	if( ! m_fd )
	{
		m_fd.reset();
		tud_dfu_finish_flashing(DFU_STATUS_ERR_FILE);
		return;
	}

	// Close file
	if(m_fd->close() < 0)
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_PROG);
		return;
	}
	m_fd.reset();

	// Write out the checksum
	{
		std::array<unsigned char, 16> md5_output;
		if(mbedtls_md5_finish_ret(&m_fd_md5_ctx, md5_output.data() ) != 0)
		{
			m_fd.reset();
			tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
			return;
		}
		mbedtls_md5_free(&m_fd_md5_ctx);

		LFS_file md5_file(&m_fs);
		if(md5_file.open("app.bin.md5.tmp", LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR) < 0)
		{
			tud_dfu_finish_flashing(DFU_STATUS_ERR_FILE);
			return;
		}

		lfs_ssize_t ret = lfs_file_write(m_fs.get_fs(), md5_file.get_fd(), md5_output.data(), md5_output.size());
		if((ret < 0) || (ret != md5_output.size()))
		{
			tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
			return;
		}

		if(md5_file.close() < 0)
		{
			tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
			return;
		}

		if(lfs_rename(m_fs.get_fs(), "app.bin.md5.tmp", "app.bin.md5") < 0)
		{
			tud_dfu_finish_flashing(DFU_STATUS_ERR_PROG);
			return;
		}

		std::array<char, 33> md5_output_hex;
		md5_output_hex.back() = '\0';
		for(size_t i = 0; i < 16; i++)
		{
			Byte_util::u8_to_hex(md5_output[i], md5_output_hex.data() + 2*i);
		}
		logger->log(LOG_LEVEL::debug, "Bootloader_task", "New firmware checksum: %s", md5_output_hex.data());
	}

	// Rename file
	if(lfs_rename(m_fs.get_fs(), "app.bin.tmp", "app.bin") < 0)
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_PROG);
		return;
	}

	logger->log(LOG_LEVEL::debug, "Bootloader_task", "New firmware written");

	tud_dfu_finish_flashing(DFU_STATUS_OK);
}
uint16_t Bootloader_task::handle_tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	if(alt != 0)
	{
		return 0;
	}

	if(block_num == 0)
	{
		if( m_fd )
		{
			m_fd.reset();
		}

		m_fd = std::make_shared<LFS_file>(&m_fs);
		if(m_fd->open("app.bin", LFS_O_RDONLY) < 0)
		{
			m_fd.reset();
			return 0;
		}
	}

	if( ! m_fd )
	{
		return 0;
	}

	// copy in
	const lfs_soff_t offset = lfs_soff_t(block_num) * lfs_soff_t(m_download_block_size);

	// Read from flash
	lfs_soff_t ret = lfs_file_seek(m_fs.get_fs(), m_fd->get_fd(), offset, LFS_SEEK_SET);
	if(ret != offset)
	{
		m_fd.reset();
		return 0;
	}

	lfs_ssize_t read_ret = lfs_file_read(m_fs.get_fs(), m_fd->get_fd(), data, length);
	if(read_ret < 0)
	{
		m_fd.reset();
		return 0;
	}

	return read_ret;
}
void Bootloader_task::handle_tud_dfu_detach_cb(void)
{
	Bootloader_key boot_key;
	boot_key.from_addr(reinterpret_cast<const uint8_t*>(0x38800000));

	boot_key.bootloader_op = uint8_t(Bootloader_key::Bootloader_ops::RUN_APP);
	boot_key.update_crc();
	boot_key.to_addr(reinterpret_cast<uint8_t volatile *>(0x38800000));

	sync_and_reset();
}
void Bootloader_task::handle_tud_dfu_abort_cb(uint8_t alt)
{
	Bootloader_key boot_key;
	boot_key.from_addr(reinterpret_cast<const uint8_t*>(0x38800000));
	
	boot_key.bootloader_op = uint8_t(Bootloader_key::Bootloader_ops::RUN_APP);
	boot_key.update_crc();
	boot_key.to_addr(reinterpret_cast<uint8_t volatile *>(0x38800000));

	sync_and_reset();
}

void Bootloader_task::work()
{
	{
		freertos_util::logging::Global_logger::set(&logging_task.get_logger());
		// freertos_util::logging::Global_logger::get()->set_sev_mask_level(LOG_LEVEL::info);
		freertos_util::logging::Global_logger::get()->set_sev_mask_level(LOG_LEVEL::debug);
		// freertos_util::logging::Global_logger::get()->set_sev_mask_level(LOG_LEVEL::TRACE);
	}

	//start logging_task
	logging_task.launch("logging", 3);

	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();
	
	logger->log(LOG_LEVEL::info, "Bootloader_task", "Started");
	logger->log(LOG_LEVEL::info, "Bootloader_task", "Version: %d.%d.%d", SW_VER_MAJOR, SW_VER_MINOR, SW_VER_PATCH);
	logger->log(LOG_LEVEL::info, "Bootloader_task", "Commit: %s", GIT_COMMIT);

	{
		std::array<char, 25> id_str;
		Bootloader_task::get_unique_id_str(&id_str);
		logger->log(LOG_LEVEL::info, "Bootloader_task", "P/N: STM32H750 Bootloader");
		logger->log(LOG_LEVEL::info, "Bootloader_task", "S/N: %s", id_str.data());

		const uint32_t idcode = DBGMCU->IDCODE;
		const uint16_t rev_id = (idcode & 0xFFFF0000) >> 16;
		const uint16_t dev_id = (idcode & 0x000007FF);
		logger->log(LOG_LEVEL::info, "Bootloader_task", "rev_id: 0x%04X", rev_id);
		logger->log(LOG_LEVEL::info, "Bootloader_task", "dev_id: 0x%04X", dev_id);
	}

	m_qspi.set_handle(&hqspi);

	if(!m_qspi.init())
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "m_qspi.init failed");

		for(;;)
		{
			vTaskSuspend(nullptr);
		}
	}

	if(0)
	{
		logger->log(LOG_LEVEL::info, "Bootloader_task", "m_qspi.cmd_chip_erase start");
		if(!m_qspi.cmd_chip_erase())
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "m_qspi.cmd_chip_erase failed");
		}
	}
	
	//verify BOR, RDP, JTAG
	{
		logger->log(LOG_LEVEL::info, "Bootloader_task", "Checking option byte");
		if(!check_option_bytes())
		{
			logger->log(LOG_LEVEL::fatal, "Bootloader_task", "Option bytes incorrect, flashing");

			if(config_option_bytes())
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Option bytes ok");
			}
			else
			{
				logger->log(LOG_LEVEL::fatal, "Bootloader_task", "Error writing option bytes");
			}
			
			sync_and_reset();
		}
		else
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "Option bytes ok");
		}
	}

	//check for software boot mode request
	Bootloader_key boot_key;
	{
		logger->log(LOG_LEVEL::info, "Bootloader_task", "Reading Boot Key");
		boot_key.from_addr(reinterpret_cast<const uint8_t*>(0x38800000));

		if(!boot_key.verify())
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "Boot Key invalid, clearing");
			boot_key = clear_bootloader_key();
		}
		else
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "Bootloader Key OK");
		}

		switch(boot_key.bootloader_op)
		{
			case uint8_t(Bootloader_key::Bootloader_ops::RUN_BOOTLDR):
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Key: RUN_BOOTLDR");
				break;
			}
			case uint8_t(Bootloader_key::Bootloader_ops::RUN_APP):
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Key: RUN_APP");
				break;
			}
			default:
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Key: unknown");
				break;
			}
		}
	}

	//check for boot mode override
	if(boot_key.bootloader_op != uint8_t(Bootloader_key::Bootloader_ops::RUN_BOOTLDR))
	{
		// Use FDCAN1 TX held low externally as a force bootloader indication
		GPIO_InitTypeDef GPIO_InitStruct = {0};
		GPIO_InitStruct.Pin = GPIO_PIN_12;
		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
		GPIO_InitStruct.Pull =  GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
		GPIO_InitStruct.Alternate = 0;
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

		vTaskDelay(1);

		size_t ctr = 0;
		for(size_t i = 0; i < 10; i++)
		{
			GPIO_PinState state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12);
			if(state == GPIO_PIN_SET)
			{
				ctr++;
			}
		}

		if(ctr < 5)
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "External request for RUN_BOOTLDR");

			boot_key.bootloader_op = uint8_t(Bootloader_key::Bootloader_ops::RUN_BOOTLDR);
			boot_key.update_crc();
		}
	}

	m_fs.initialize();
	m_fs.set_flash(&m_qspi);

	logger->log(LOG_LEVEL::info, "Bootloader_task", "Mounting flash fs");
	int mount_ret = m_fs.mount();
	if(mount_ret != LFS_ERR_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Flash mount failed: %d", mount_ret);
		logger->log(LOG_LEVEL::error, "Bootloader_task", "You will need to reload the firmware");

		logger->log(LOG_LEVEL::info, "Bootloader_task", "Format flash");
		int format_ret = m_fs.format();
		if(format_ret != LFS_ERR_OK)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Flash format failed: %d", format_ret);
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Try a power cycle, your board may be broken");
			for(;;)
			{
				vTaskSuspend(nullptr);
			}
		}

		logger->log(LOG_LEVEL::info, "Bootloader_task", "Mounting flash fs");
		mount_ret = m_fs.mount();
		if(mount_ret != LFS_ERR_OK)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Flash mount failed right after we formatted it: %d", mount_ret);
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Try a power cycle, your board may be broken");
			for(;;)
			{
				vTaskSuspend(nullptr);
			}
		}
	}

	logger->log(LOG_LEVEL::info, "Bootloader_task", "Flash mount ok");

	switch(boot_key.bootloader_op)
	{
		case uint8_t(Bootloader_key::Bootloader_ops::RUN_BOOTLDR):
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "Bootloader requested");
			break;
		}
		case uint8_t(Bootloader_key::Bootloader_ops::RUN_APP):
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "App load requested");

			logger->log(LOG_LEVEL::info, "Bootloader_task", "Looking for bin gcm file");
			if(load_verify_bin_gcm_app_image())
			{
				for(;;)
				{

				}
			}
			else
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Looking for hex file");
				if(load_verify_hex_app_image())
				{
					for(;;)
					{

					}
				}
				else 
				{
					logger->log(LOG_LEVEL::info, "Bootloader_task", "Looking for bin file");
					if(load_verify_bin_app_image())
					{
						for(;;)
						{

						}					
					}
					else
					{
						logger->log(LOG_LEVEL::info, "Bootloader_task", "App load failed, staying in bootloader mode");
					}
				}
			}

			break;
		}
		default:
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "Invalid boot key");
			break;
		}
	}

	logger->log(LOG_LEVEL::info, "Bootloader_task", "Starting usb");
	led_task.set_state(LED_task::LED_STATE::WAIT_FOR_HOST);
	init_usb();

	for(;;)
	{
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

bool Bootloader_task::load_verify_hex_app_image()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	const char* fname = "app.hex";
	lfs_file_t fd = { };
	int ret = lfs_file_open(m_fs.get_fs(), &fd, fname, LFS_O_RDONLY);
	if(ret < 0)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, fname, ret);
		return false;
	}

	mbedtls_md5_context md5_ctx;
	mbedtls_md5_init(&md5_ctx);

	mbedtls_md5_starts_ret(&md5_ctx);

	//stream buffer for line extraction
	std::vector<char> file_buffer;
	file_buffer.reserve(1024);

	Stack_string<128> line_buffer;
	// line_buffer.reserve(64);

	//256 byte read buffer
	std::vector<char> read_buffer;
	read_buffer.resize(256);

	Intel_hex_loader hex_loader;

	lfs_ssize_t read_ret = 0;
	do
	{
		read_ret = lfs_file_read(m_fs.get_fs(), &fd, read_buffer.data(), read_buffer.size());
		if(read_ret < 0)
		{
			return false;
		}

		mbedtls_md5_update_ret(&md5_ctx, (unsigned char*) read_buffer.data(), read_ret);

		file_buffer.insert(file_buffer.end(), read_buffer.begin(), std::next(read_buffer.begin(), read_ret));

		std::vector<char>::iterator line_it;
		do
		{
			line_it = std::find(file_buffer.begin(), file_buffer.end(), '\n');
			if(line_it == file_buffer.end())
			{
				break;
			}

			//copy the line
			auto line_next_it = std::next(line_it);
			line_buffer.assign(file_buffer.begin(), line_next_it);
			file_buffer.erase(file_buffer.begin(), line_next_it);

			if(!hex_loader.process_line(line_buffer.data(), line_buffer.size()))
			{
				logger->log(LOG_LEVEL::error, "Bootloader_task", "hex_loader.process_line failed: %s", line_buffer.c_str());
				break;
			}

		} while(line_it != file_buffer.end());
	} while(read_ret > 0);
	
	int close_ret = lfs_file_close(m_fs.get_fs(), &fd);
	if(close_ret != LFS_ERR_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, fname, close_ret);
	}

	logger->log(LOG_LEVEL::info, "Bootloader_task", "File loaded");

	std::array<unsigned char, 16> md5_output;
	mbedtls_md5_finish_ret(&md5_ctx, md5_output.data() );
	mbedtls_md5_free(&md5_ctx);

	std::array<char, 33> md5_output_hex;
	md5_output_hex.back() = '\0';
	for(size_t i = 0; i < 16; i++)
	{
		Byte_util::u8_to_hex(md5_output[i], md5_output_hex.data() + 2*i);
	}
	logger->log(LOG_LEVEL::debug, "Bootloader_task", "File checksum: %s", md5_output_hex.data());

	if(hex_loader.has_eof())
	{
		uint32_t boot_addr = 0;
		if(!hex_loader.get_boot_addr(&boot_addr))
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Got EOF, but no boot addr");
			return false;
		}

		logger->log(LOG_LEVEL::debug, "Bootloader_task", "Got EOF, boot addr: 0x%08" PRIX32 ", estack: %08" PRIX32, boot_addr, 0);
		logger->log(LOG_LEVEL::info, "Bootloader_task", "Jumping to application...");

		jump_to_addr(0, boot_addr);

		//we should never reach here
		for(;;)
		{

		}
	}
	else
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "No EOF in boot record");
		return false;
	}

	return true;
}

bool Bootloader_task::load_verify_bin_app_image()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	const char* fname = "app.bin";
	lfs_file_t fd = { };
	int ret = lfs_file_open(m_fs.get_fs(), &fd, fname, LFS_O_RDONLY);
	if(ret < 0)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, fname, ret);
		return false;
	}

	mbedtls_md5_context md5_ctx;
	mbedtls_md5_init(&md5_ctx);

	mbedtls_md5_starts_ret(&md5_ctx);

	//256 byte read buffer
	std::vector<char> read_buffer;
	read_buffer.resize(256);

	volatile uint8_t* const axi_base = reinterpret_cast<volatile uint8_t*>(0x24000000);
	size_t num_written = 0;

	lfs_ssize_t read_ret = 0;
	do
	{
		read_ret = lfs_file_read(m_fs.get_fs(), &fd, read_buffer.data(), read_buffer.size());
		if(read_ret < 0)
		{
			return false;
		}

		mbedtls_md5_update_ret(&md5_ctx, (unsigned char*) read_buffer.data(), read_ret);

		std::copy_n(read_buffer.data(), read_ret, axi_base + num_written);
		num_written += read_ret;

	} while(read_ret > 0);
	
	int close_ret = lfs_file_close(m_fs.get_fs(), &fd);
	if(close_ret != LFS_ERR_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, fname, close_ret);
	}

	logger->log(LOG_LEVEL::info, "Bootloader_task", "File loaded");

	std::array<unsigned char, 16> md5_output;
	mbedtls_md5_finish_ret(&md5_ctx, md5_output.data() );
	mbedtls_md5_free(&md5_ctx);

	std::array<char, 33> md5_output_hex;
	md5_output_hex.back() = '\0';
	for(size_t i = 0; i < 16; i++)
	{
		Byte_util::u8_to_hex(md5_output[i], md5_output_hex.data() + 2*i);
	}
	logger->log(LOG_LEVEL::debug, "Bootloader_task", "File checksum: %s", md5_output_hex.data());

	uint32_t app_estack = 0;
	uint32_t app_reset_handler = 0;

	std::copy_n(axi_base, sizeof(app_estack), reinterpret_cast<uint8_t*>(&app_estack));
	std::copy_n(axi_base + sizeof(app_estack), sizeof(app_reset_handler), reinterpret_cast<uint8_t*>(&app_reset_handler));

	logger->log(LOG_LEVEL::debug, "Bootloader_task", "Got EOF, boot addr: 0x%08" PRIX32 ", estack: %08" PRIX32, app_reset_handler, app_estack);
	logger->log(LOG_LEVEL::info, "Bootloader_task", "Jumping to application...");

	jump_to_addr(app_estack, app_reset_handler);

	//we should never reach here
	for(;;)
	{

	}

	return true;
}

bool Bootloader_task::load_verify_bin_gcm_app_image()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	const char* appfname = "app.bin.enc";
	const char* appauxfname = "app.bin.enc.xml";
	lfs_file_t fd = { };
	int ret = lfs_file_open(m_fs.get_fs(), &fd, appfname, LFS_O_RDONLY);
	if(ret < 0)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, appfname, ret);
		return false;
	}

	AES_GCM_aux_data aux_data;
	{
		lfs_file_t fd_aux = { };
		int ret_aux = lfs_file_open(m_fs.get_fs(), &fd_aux, appauxfname, LFS_O_RDONLY);
		if(ret_aux < 0)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, appauxfname, ret_aux);
			return false;
		}

		lfs_info app_aux_stat = { };
		int ret = lfs_stat(m_fs.get_fs(), appauxfname, &app_aux_stat);
		if(ret != LFS_ERR_OK)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "fstat file %s failed: %" PRId32, appauxfname, ret);
			return false;
		}

		if(app_aux_stat.type != LFS_TYPE_REG)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "file %s not regular file", appauxfname, ret);
			return false;
		}

		std::vector<char> aux_data_file(app_aux_stat.size);
		int read_ret = lfs_file_read(m_fs.get_fs(), &fd_aux, aux_data_file.data(), aux_data_file.size());
		if(read_ret < 0)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "file %s read failed: %" PRId32, appauxfname, read_ret);
			return false;
		}
		else if(size_t(read_ret) != aux_data_file.size())
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "file %s read failed: %" PRId32, appauxfname, read_ret);
			return false;
		}

		tinyxml2::XMLDocument doc;
		if(doc.Parse(aux_data_file.data(), aux_data_file.size()) != tinyxml2::XML_SUCCESS)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "file %s xml parse failed", appauxfname);
			return false;
		}

		if(!aux_data.from_xml(doc))
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "file %s xml load failed", appauxfname);
			return false;	
		}

		int close_ret = lfs_file_close(m_fs.get_fs(), &fd_aux);
		if(close_ret != LFS_ERR_OK)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, appauxfname, close_ret);
		}
	}
	

	///
	volatile uint8_t* const axi_base = reinterpret_cast<volatile uint8_t*>(0x24000000);
	{
		mbed_aes128_gcm_dec gcm_dec;
		gcm_dec.set_key(bootloader_key);//compiled in global
		gcm_dec.set_iv(aux_data.get_iv());//loaded from xml
		gcm_dec.set_tag(aux_data.get_tag());//loaded from xml

		gcm_dec.initialize(nullptr, 0);

		mbed_aes128_gcm::BlockType in_block;
		mbed_aes128_gcm::BlockType out_block;

		size_t num_written = 0;
		int read_ret = 0;
		do
		{
			read_ret = lfs_file_read(m_fs.get_fs(), &fd, in_block.data(), in_block.size());
			if(read_ret < 0)
			{
				return false;
			}

			size_t out_len = 0;
			if(!gcm_dec.update(in_block, read_ret, &out_block, &out_len))
			{
				logger->log(LOG_LEVEL::error, "Bootloader_task", "GCM update failed");
				return false;
			}

			std::copy_n(out_block.data(), out_len, axi_base + num_written);
			num_written += out_len;

		} while(read_ret > 0);

		size_t out_len = 0;
		int mbedtls_ret = 0;
		if(!gcm_dec.finish(&out_block, &out_len, &mbedtls_ret))
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "GCM finish failed");
			return false;
		}

		std::copy_n(out_block.data(), out_len, axi_base + num_written);
		num_written += out_len;
	}
	///

	int close_ret = lfs_file_close(m_fs.get_fs(), &fd);
	if(close_ret != LFS_ERR_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, appfname, close_ret);
	}

	logger->log(LOG_LEVEL::info, "Bootloader_task", "File loaded");

	{
		uint32_t app_estack = 0;
		uint32_t app_reset_handler = 0;

		std::copy_n(axi_base, sizeof(app_estack), reinterpret_cast<uint8_t*>(&app_estack));
		std::copy_n(axi_base + sizeof(app_estack), sizeof(app_reset_handler), reinterpret_cast<uint8_t*>(&app_reset_handler));

		logger->log(LOG_LEVEL::debug, "Bootloader_task", "Got EOF, boot addr: 0x%08" PRIX32 ", estack: %08" PRIX32, app_reset_handler, app_estack);
		logger->log(LOG_LEVEL::info, "Bootloader_task", "Jumping to application...");

		jump_to_addr(app_estack, app_reset_handler);
	}

	//we should never reach here
	for(;;)
	{

	}

	return false;
}

void Bootloader_task::jump_to_addr(uint32_t estack, uint32_t jump_addr)
{
	//Disable ISR, sync
	asm volatile(
		"cpsid i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	//Disable Cache
	SCB_DisableDCache();
	SCB_DisableICache();

	//Invalidate Cache
	// SCB_InvalidateDCache();
	// SCB_InvalidateICache();

	//Sync
	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	//http://www.keil.com/support/docs/3913.htm

	NVIC->ICER[ 0 ] = 0xFFFFFFFF;
	NVIC->ICER[ 1 ] = 0xFFFFFFFF;
	NVIC->ICER[ 2 ] = 0xFFFFFFFF;
	NVIC->ICER[ 3 ] = 0xFFFFFFFF;
	NVIC->ICER[ 4 ] = 0xFFFFFFFF;
	NVIC->ICER[ 5 ] = 0xFFFFFFFF;
	NVIC->ICER[ 6 ] = 0xFFFFFFFF;
	NVIC->ICER[ 7 ] = 0xFFFFFFFF;

	//disable and reset peripherals
	__HAL_RCC_AHB1_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_AHB1_RELEASE_RESET();
	__HAL_RCC_AHB2_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_AHB2_RELEASE_RESET();

	//we can't bulk reset AHB3 because that resets the cpu and fmc
	// __HAL_RCC_AHB3_FORCE_RESET();
	// __HAL_RCC_AHB3_RELEASE_RESET();
	__HAL_RCC_MDMA_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_MDMA_RELEASE_RESET();
	__HAL_RCC_DMA2D_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_DMA2D_RELEASE_RESET();
	__HAL_RCC_JPGDECRST_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_JPGDECRST_RELEASE_RESET();
	// __HAL_RCC_FMC_FORCE_RESET();
	// __HAL_RCC_FMC_RELEASE_RESET();
	__HAL_RCC_QSPI_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_QSPI_RELEASE_RESET();
	__HAL_RCC_SDMMC1_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_SDMMC1_RELEASE_RESET();
	// __HAL_RCC_CPU_FORCE_RESET();
	// __HAL_RCC_CPU_RELEASE_RESET();

	__HAL_RCC_AHB4_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_AHB4_RELEASE_RESET();

	__HAL_RCC_APB1L_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB1L_RELEASE_RESET();
	__HAL_RCC_APB1H_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB1H_RELEASE_RESET();
	__HAL_RCC_APB2_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB2_RELEASE_RESET();
	__HAL_RCC_APB3_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB3_RELEASE_RESET();
	__HAL_RCC_APB4_FORCE_RESET();
	asm volatile("dsb sy\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB4_RELEASE_RESET();

	//Sync
	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	NVIC->ICPR[ 0 ] = 0xFFFFFFFF;
	NVIC->ICPR[ 1 ] = 0xFFFFFFFF;
	NVIC->ICPR[ 2 ] = 0xFFFFFFFF;
	NVIC->ICPR[ 3 ] = 0xFFFFFFFF;
	NVIC->ICPR[ 4 ] = 0xFFFFFFFF;
	NVIC->ICPR[ 5 ] = 0xFFFFFFFF;
	NVIC->ICPR[ 6 ] = 0xFFFFFFFF;
	NVIC->ICPR[ 7 ] = 0xFFFFFFFF;

	HAL_RCC_DeInit();

	SysTick->CTRL = 0;
	SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;

	//TODO - scrub ram?

	//Disable MPU
	HAL_MPU_Disable();

	//Sync
	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	__set_CONTROL(0);
	__set_MSP(estack);

	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		"bx %[jump]\n"
	: /* no out */
	: [jump] "r" (jump_addr)
	: "memory"
	);

	//should never reach here
	for(;;)
	{

	}
}


bool Bootloader_task::check_option_bytes()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	FLASH_OBProgramInitTypeDef ob_init = { };
	ob_init.Banks = FLASH_BANK_1;
	HAL_FLASHEx_OBGetConfig(&ob_init);

	bool ret = true;

	const uint32_t NEEDED_OB = OPTIONBYTE_WRP | OPTIONBYTE_RDP | OPTIONBYTE_BOR;
	if((ob_init.OptionType & NEEDED_OB) != NEEDED_OB)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "Returned OptionType incorrect");
		ret = false;
	}
	else
	{
		if(ob_init.WRPState != OB_WRPSTATE_DISABLE)
		{
			logger->log(LOG_LEVEL::fatal, "Bootloader_task", "WRPState incorrect");
			ret = false;
		}

		// ob_init.WRPSector seems to be read back as 0 when WRPState is OB_WRPSTATE_DISABLE
		// if(ob_init.WRPSector != FLASH_BANK_1)
		// {
		// 	logger->log(LOG_LEVEL::fatal, "Bootloader_task", "WRPSector incorrect");
		// 	ret = false;
		// }

		if(ob_init.RDPLevel != OB_RDP_LEVEL_0)
		{
			logger->log(LOG_LEVEL::fatal, "Bootloader_task", "RDPLevel incorrect");
			ret = false;
		}

		if(ob_init.BORLevel != OB_BOR_LEVEL3)
		{
			logger->log(LOG_LEVEL::fatal, "Bootloader_task", "BORLevel incorrect");
			ret = false;
		}
	}

	return ret;
}

bool Bootloader_task::config_option_bytes()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	FLASH_OBProgramInitTypeDef ob_init = { };
	ob_init.Banks = FLASH_BANK_1;
	HAL_FLASHEx_OBGetConfig(&ob_init);

	HAL_StatusTypeDef ret = HAL_FLASH_Unlock();
	if(ret != HAL_OK)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "HAL_FLASH_Unlock failed");
		return false;
	}

	ret = HAL_FLASH_OB_Unlock();
	if(ret != HAL_OK)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "HAL_FLASH_OB_Unlock failed");
		return false;
	}

	ob_init.OptionType = 0;
	ob_init.Banks = FLASH_BANK_1;
	// ob_init.BootConfig
	// ob_init.BootAddr0
	// ob_init.BootAddr1

	if(ob_init.RDPLevel != OB_RDP_LEVEL_0)
	{
		ob_init.OptionType |= OPTIONBYTE_RDP;
		ob_init.RDPLevel    = OB_RDP_LEVEL_0;
	}

	if(ob_init.WRPState != OB_WRPSTATE_DISABLE)
	{
		ob_init.OptionType |= OPTIONBYTE_WRP;
		ob_init.WRPState    = OB_WRPSTATE_DISABLE;
		ob_init.WRPSector   = FLASH_BANK_1;
	}

	if(ob_init.BORLevel != OB_BOR_LEVEL3)
	{
		ob_init.OptionType |= OPTIONBYTE_BOR;
		ob_init.BORLevel    = OB_BOR_LEVEL3;
	}
	
	// ob_init.OptionType |= OPTIONBYTE_USER;
	// ob_init.USERType;
	// ob_init.USERConfig = RST_STOP

	// ob_init.OptionType |= OPTIONBYTE_PCROP;
	// ob_init.PCROPConfig = OB_PCROP_RDP_ERASE;
	// ob_init.PCROPStartAddr
	// ob_init.PCROPEndAddr
	
	// ob_init.OptionType |= OPTIONBYTE_SECURE_AREA;
	// ob_init.SecureAreaConfig = OB_SECURE_RDP_ERASE;
	// ob_init.SecureAreaStartAddr
	// ob_init.SecureAreaEndAddr

	// FLASH->SR1 = 0;
	// FLASH->SR2 = 0;
	HAL_StatusTypeDef prog_ret = HAL_FLASHEx_OBProgram(&ob_init);
	if(prog_ret != HAL_OK)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "HAL_FLASHEx_OBProgram failed");
		return false;
	}

	HAL_StatusTypeDef launch_ret = HAL_FLASH_OB_Launch();
	if(launch_ret != HAL_OK)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "HAL_FLASH_OB_Launch failed");
		return false;
	}

	ret = HAL_FLASH_OB_Lock();
	if(ret != HAL_OK)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "HAL_FLASH_OB_Lock failed");
		return false;
	}

	ret = HAL_FLASH_Lock();
	if(ret != HAL_OK)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "HAL_FLASH_Lock failed");
		return false;
	}

	return true;
}

bool Bootloader_task::init_usb()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	//set id
	get_unique_id_str(&usb_id_str);

	// config gpio
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	/**USB_OTG_HS GPIO Configuration    
	PC0     ------> USB_OTG_HS_ULPI_STP
	PC2_C     ------> USB_OTG_HS_ULPI_DIR
	PC3_C     ------> USB_OTG_HS_ULPI_NXT
	PA3     ------> USB_OTG_HS_ULPI_D0
	PA5     ------> USB_OTG_HS_ULPI_CK
	PB0     ------> USB_OTG_HS_ULPI_D1
	PB1     ------> USB_OTG_HS_ULPI_D2
	PB10     ------> USB_OTG_HS_ULPI_D3
	PB11     ------> USB_OTG_HS_ULPI_D4
	PB12     ------> USB_OTG_HS_ULPI_D5
	PB13     ------> USB_OTG_HS_ULPI_D6
	PB5     ------> USB_OTG_HS_ULPI_D7 
	*/
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_5;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11 
	                      |GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_5;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	__HAL_RCC_USB_OTG_HS_CLK_ENABLE();
	__HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();

	// Start tinyusb
	{
		NVIC_SetPriority(OTG_HS_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
		// NVIC_EnableIRQ(OTG_HS_IRQn);

		tusb_rhport_init_t dev_init = {
			.role = TUSB_ROLE_DEVICE,
			.speed = TUSB_SPEED_AUTO
		};
		tusb_init(1, &dev_init);
	}

	//process usb packets
	usb_core_task.launch("usb_core", 4);

	return true;
}

void Bootloader_task::get_unique_id(std::array<uint32_t, 3>* const id)
{
	volatile uint32_t* addr = reinterpret_cast<uint32_t*>(0x1FF1E800);

	std::copy_n(addr, 3, id->data());
}

void Bootloader_task::get_unique_id_str(std::array<char, 25>* const id_str)
{
	//0x012345670123456701234567
	std::array<uint32_t, 3> id;
	get_unique_id(&id);

	snprintf(id_str->data(), id_str->size(), "%08" PRIX32 "%08" PRIX32 "%08" PRIX32, id[0], id[1], id[2]);
}

void Bootloader_task::sync_and_reset()
{
	asm volatile(
		"cpsid i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	SCB_DisableDCache();
	SCB_DisableICache();
	
	NVIC_SystemReset();

	for(;;)
	{

	}	
}
