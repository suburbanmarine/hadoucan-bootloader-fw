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

uint8_t* const Bootloader_task::m_axi_mem_base = reinterpret_cast<uint8_t*>(0x24000000);

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
		tud_dfu_finish_flashing(DFU_STATUS_ERR_UNKNOWN);
		return;
	}

	if(block_num == 0)
	{
		if( m_fd )
		{
			m_fd.reset();
		}

		// delete old fw first, in case there is not enough space for a new image
		// originally, I wanted to do an atomic rename to app.bin to preserve it on failed flash
		// in practice, this gets wedged if someone fills the flash with a too-large app.bin accidentally
		{
			if( ! delete_file_if_exists("app.bin.md5") )
			{
				tud_dfu_finish_flashing(DFU_STATUS_ERR_ERASE);
				return;			
			}

			if( ! delete_file_if_exists("app.bin.md5.tmp") )
			{
				tud_dfu_finish_flashing(DFU_STATUS_ERR_ERASE);
				return;			
			}

			if( ! delete_file_if_exists("app.bin") )
			{
				tud_dfu_finish_flashing(DFU_STATUS_ERR_ERASE);
				return;			
			}
		}

		m_fd = std::make_shared<LFS_file>(&m_fs);
		if(m_fd->open("app.bin.tmp", LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR) < 0)
		{
			m_fd.reset();
			tud_dfu_finish_flashing(DFU_STATUS_ERR_ERASE);
			return;
		}

		m_fd_md5_ctx = std::make_shared<mbedtls_md5_helper>();
		if(mbedtls_md5_starts_ret(m_fd_md5_ctx->get()) != 0)
		{
			m_fd.reset();
			tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
		}
	}

	if( ! m_fd )
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_UNKNOWN);
		return;
	}

	// copy in
	const size_t offset = size_t(block_num) * m_download_block_size;

	if((offset + length) > m_mem_size)
	{
		tud_dfu_finish_flashing(DFU_STATUS_ERR_ADDRESS);
		return;
	}

	memcpy(m_axi_mem_base + offset, data, length);

	if(mbedtls_md5_update_ret(m_fd_md5_ctx->get(), data, length) != 0)
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
		tud_dfu_finish_flashing(DFU_STATUS_ERR_UNKNOWN);
		return;
	}

	if( ! m_fd )
	{
		m_fd.reset();
		tud_dfu_finish_flashing(DFU_STATUS_ERR_UNKNOWN);
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
	std::array<unsigned char, 16> md5_output;
	{
		if(mbedtls_md5_finish_ret(m_fd_md5_ctx->get(), md5_output.data() ) != 0)
		{
			m_fd.reset();
			tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
			return;
		}

		LFS_file md5_file(&m_fs);
		if(md5_file.open("app.bin.md5.tmp", LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR) < 0)
		{
			tud_dfu_finish_flashing(DFU_STATUS_ERR_ERASE);
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

	//TODO: read back app.bin and verify checksum?
	{
		std::array<uint8_t, 16> read_back_md5;
		if( ! calc_file_md5("app.bin", &read_back_md5) )
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Firmware readback md5 calc failed");

			tud_dfu_finish_flashing(DFU_STATUS_ERR_PROG);
			return;
		}

		if( ! std::equal(md5_output.begin(), md5_output.end(), read_back_md5.begin(), read_back_md5.end()) )
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Firmware readback md5 mismatch");

			tud_dfu_finish_flashing(DFU_STATUS_ERR_PROG);
			return;
		}
	}

	logger->log(LOG_LEVEL::debug, "Bootloader_task", "New firmware written");

	tud_dfu_finish_flashing(DFU_STATUS_OK);
}

bool Bootloader_task::calc_file_md5(const char* path, std::array<uint8_t, 16>* out_md5)
{
	LFS_file app_file(&m_fs);
	if(app_file.open(path, LFS_O_RDONLY) < 0)
	{
		return false;
	}

	std::shared_ptr<mbedtls_md5_helper> md5_ctx = std::make_shared<mbedtls_md5_helper>();
	mbedtls_md5_init(md5_ctx->get());
	if(mbedtls_md5_starts_ret(md5_ctx->get()) != 0)
	{
		return false;
	}

	std::vector<uint8_t> buf;
	buf.resize(512);

	lfs_ssize_t num_read = 0;
	do
	{
		num_read = lfs_file_read(m_fs.get_fs(), app_file.get_fd(), buf.data(), buf.size());
		if(num_read < 0)
		{
			return false;
		}

		if(mbedtls_md5_update_ret(md5_ctx->get(), buf.data(), num_read) != 0)
		{
			return false;
		}
	} while(num_read > 0);

	if(mbedtls_md5_finish_ret(md5_ctx->get(), out_md5->data() ) != 0)
	{
		return false;
	}

	return true;
}

uint16_t Bootloader_task::handle_tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	char const * file_name;

	switch(alt)
	{
		case 0:
		{
			file_name = "app.bin";
		}
		case 1:
		{
			file_name = "app.bin.md5";
		}
		default:
		{
			return 0;
		}
	}

	if(block_num == 0)
	{
		if( m_fd )
		{
			m_fd.reset();
		}

		m_fd = std::make_shared<LFS_file>(&m_fs);
		if(m_fd->open(file_name, LFS_O_RDONLY) < 0)
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
	set_bootloader_key(Bootloader_key::Bootloader_ops::LOAD_APP);

	sync_and_reset();
}
void Bootloader_task::handle_tud_dfu_abort_cb(uint8_t alt)
{
	set_bootloader_key(Bootloader_key::Bootloader_ops::LOAD_APP);

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
		boot_key.from_addr(reinterpret_cast<uint8_t const *>(0x38800000));

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
			case uint8_t(Bootloader_key::Bootloader_ops::LOAD_APP):
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Key: LOAD_APP");
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
		case uint8_t(Bootloader_key::Bootloader_ops::LOAD_APP):
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "App load requested, clearing axi sram");
			zero_axi_sram();

			/*
			logger->log(LOG_LEVEL::info, "Bootloader_task", "Looking for bin gcm file");
			if(load_verify_bin_gcm_app_image())
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "App load complete, resetting");
				std::array<uint8_t, 16> md5_axi = calculate_md5_axi_sram();
				set_bootloader_key(Bootloader_key::Bootloader_ops::RUN_APP, md5_axi);

				// See 2.4 Embedded SRAM note Error code correction
				// Write are delayed in the event of a less than ecc sized write
				// Flush SRAM areas needed to be preserved before resetting
				ecc_flush_axi_sram(key.app_length);
				
				sync_and_reset();

				for(;;)
				{

				}
			}
			else
			*/
			{
				Bootloader_key key;
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Looking for bin file");
				if(load_verify_bin_app_image(&key))
				{
					logger->log(LOG_LEVEL::info, "Bootloader_task", "App load complete, resetting");

					// See 2.4 Embedded SRAM note Error code correction
					// Write are delayed in the event of a less than ecc sized write
					// Flush SRAM areas needed to be preserved before resetting
					ecc_flush_axi_sram(key.app_length);

					sync_and_reset();

					for(;;)
					{

					}					
				}
				else
				{
					logger->log(LOG_LEVEL::info, "Bootloader_task", "App load failed, staying in bootloader mode");
				}
			}
			break;
		}
		case uint8_t(Bootloader_key::Bootloader_ops::RUN_APP):
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "App run requested, but we got to the main Bootloader task for some reason");
			logger->log(LOG_LEVEL::error, "Bootloader_task", "The app should have been jumped to close to the start of main");
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Restarting");
			sync_and_reset();
			break;
		}
		default:
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Invalid boot key, staying in bootloader mode");
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

bool Bootloader_task::load_verify_bin_app_image(Bootloader_key* const key)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	const char* fname = "app.bin";
	LFS_file file(&m_fs);
	int ret = file.open(fname, LFS_O_RDONLY);
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

	volatile uint8_t* const axi_base = m_axi_mem_base;
	size_t num_written = 0;

	lfs_ssize_t read_ret = 0;
	do
	{
		read_ret = lfs_file_read(m_fs.get_fs(), file.get_fd(), read_buffer.data(), read_buffer.size());
		if(read_ret < 0)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Error loading app file");
			return false;
		}

		mbedtls_md5_update_ret(&md5_ctx, (unsigned char*) read_buffer.data(), read_ret);

		std::copy_n(read_buffer.data(), read_ret, axi_base + num_written);
		num_written += read_ret;

	} while(read_ret > 0);
	
	int close_ret = file.close();
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

	std::array<unsigned char, 16> md5_input;
	{
		LFS_file md5_file(&m_fs);
		ret = md5_file.open("app.bin.md5", LFS_O_RDONLY);
		if(ret < 0)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Error loading app file checksum");
			return false;
		}

		read_ret = lfs_file_read(m_fs.get_fs(), md5_file.get_fd(), md5_input.data(), md5_input.size());
		if(read_ret != md5_input.size())
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Error loading app file checksum");
			return false;
		}

		md5_file.close();

		if( ! std::equal(md5_output.begin(), md5_output.end(), md5_input.begin()) )
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "File checksum match fail");
			return false;
		}

		logger->log(LOG_LEVEL::debug, "Bootloader_task", "File checksum match ok");
	}

	*key = set_bootloader_key(Bootloader_key::Bootloader_ops::RUN_APP, md5_input, num_written);

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
	volatile uint8_t* const axi_base = m_axi_mem_base;
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

	return true;
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

void Bootloader_task::zero_axi_sram()
{
	uint64_t volatile* const axi_base = reinterpret_cast<uint64_t volatile*>(m_axi_mem_base);

	std::fill_n(axi_base, m_mem_size / 8, 0);

	__DSB();
}

void Bootloader_task::ecc_flush_axi_sram(const uint32_t offset)
{
	const size_t offset_in_words = offset / 8UL;

	asm volatile(
		"cpsid i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);

	SCB_DisableDCache();

	uint64_t volatile* const axi_base = reinterpret_cast<uint64_t volatile*>(m_axi_mem_base);
	uint64_t tmp = axi_base[offset_in_words];
	axi_base[offset_in_words] = tmp;

	if(offset_in_words > 1)
	{
		tmp = axi_base[offset_in_words-1];
		axi_base[offset_in_words-1] = tmp;
	}

	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);

	SCB_EnableDCache();

	asm volatile(
		"cpsie i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);
}

void Bootloader_task::ecc_flush_bbram_noisr_noenable(const uint32_t offset)
{
	const size_t offset_in_words = offset / 4UL;

	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);

	SCB_DisableDCache();

	uint32_t volatile* const bbram_base = reinterpret_cast<uint32_t volatile*>(0x38800000);
	uint32_t tmp = bbram_base[offset_in_words];
	bbram_base[offset_in_words] = tmp;

	if(offset_in_words > 1)
	{
		tmp = bbram_base[offset_in_words-1];
		bbram_base[offset_in_words-1] = tmp;
	}

	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);

	SCB_EnableDCache();
}

void Bootloader_task::ecc_flush_bbram(const uint32_t offset)
{
	const size_t offset_in_words = offset / 4UL;

	asm volatile(
		"cpsid i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);

	SCB_DisableDCache();

	HAL_PWR_EnableBkUpAccess();

	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);

	uint32_t volatile* const bbram_base = reinterpret_cast<uint32_t volatile*>(0x38800000);
	uint32_t tmp = bbram_base[offset_in_words];
	bbram_base[offset_in_words] = tmp;

	asm volatile(
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);

	HAL_PWR_DisableBkUpAccess();

	SCB_EnableDCache();

	asm volatile(
		"cpsie i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
	);
}

std::array<uint8_t, 16> Bootloader_task::calculate_md5_axi_sram(const uint32_t length)
{
	uint8_t* const axi_base = reinterpret_cast<uint8_t*>(m_axi_mem_base);

	std::array<uint8_t, 16> md5_output;
	
	mbedtls_md5_context md5_ctx;
	mbedtls_md5_init(&md5_ctx);
	mbedtls_md5_starts_ret(&md5_ctx);
	mbedtls_md5_update_ret(&md5_ctx, axi_base, std::min<size_t>(length, m_mem_size));
	mbedtls_md5_finish_ret(&md5_ctx, md5_output.data());
	mbedtls_md5_free(&md5_ctx);

	return md5_output;
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

bool Bootloader_task::delete_file_if_exists(const char* path)
{
	lfs_info info;
	int ret = lfs_stat(m_fs.get_fs(), path, &info);
	if(ret == LFS_ERR_NOENT)
	{
		return true;
	}
	else if(ret != LFS_ERR_OK)
	{
		return false;
	}

	switch(info.type)
	{
		case LFS_TYPE_DIR:
		{
			// TODO: add recursive option?
			return false;
		}
		case LFS_TYPE_REG:
		{
			ret = lfs_remove(m_fs.get_fs(), path);
		}
		default:
		{
			return false;
		}
	}

	return ret == LFS_ERR_OK;
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
