#include "Bootloader_task.hpp"

#include "bootloader_aes_gcm_key.hpp"

#include "freertos_cpp_util/logging/Global_logger.hpp"

#include "global_inst.hpp"
#include "sw_ver.hpp"

#include "hal_inst.h"
#include "stm32h7xx_hal.h"

#include "bootloader_util/Intel_hex_loader.hpp"
#include "common_util/Byte_util.hpp"

#include "mbedtls_util/AES_GCM_aux_data.hpp"
#include "mbedtls_util/mbed_aes128_gcm_dec.hpp"

#include "freertos_cpp_util/logging/Global_logger.hpp"

#include "USB_rx_buffer_task.hpp"
#include "USB_poll.hpp"
#include "Logging_task.hpp"

#include <mbedtls/md5.h>

#include <cctype>
#include <cinttypes>
#include <functional>

using freertos_util::logging::LOG_LEVEL;

USB_core_task      usb_core_task      __attribute__ (( section(".ram_dtcm_noload") ));
USB_rx_buffer_task usb_rx_buffer_task __attribute__ (( section(".ram_dtcm_noload") ));
Logging_task       logging_task       __attribute__(( section(".ram_d2_s1_noload") ));

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
	if(1)
	{
		logger->log(LOG_LEVEL::info, "Bootloader_task", "Checking option byte");
		if(!check_option_bytes())
		{
			logger->log(LOG_LEVEL::fatal, "Bootloader_task", "Option bytes incorrect, flashing");

			if( (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0)
			{
				logger->log(LOG_LEVEL::fatal, "Bootloader_task", "JTAG attatched, cannot continue");
				for(;;)
				{
					vTaskSuspend(nullptr);
				}
			}

			if(config_option_bytes())
			{
				logger->log(LOG_LEVEL::info, "Bootloader_task", "Option bytes ok");
			}
			else
			{
				logger->log(LOG_LEVEL::fatal, "Bootloader_task", "Error writing option bytes");
			}
			
			{
				asm volatile(
					"cpsid i\n"
					"dsb 0xF\n"
					"isb 0xF\n"
					: /* no out */
					: /* no in */
					: "memory"
				);

				SCB_DisableDCache();
				SCB_DisableICache();

				NVIC_SystemReset();
			}
		
			for(;;)
			{

			}
		}
		else
		{
			logger->log(LOG_LEVEL::info, "Bootloader_task", "Option bytes ok");
		}
	}
	else
	{
		logger->log(LOG_LEVEL::warn, "Bootloader_task", "Skipping option byte check");
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
	if(mount_ret != SPIFFS_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Flash mount failed: %d", mount_ret);
		logger->log(LOG_LEVEL::error, "Bootloader_task", "You will need to reload the firmware");

		logger->log(LOG_LEVEL::info, "Bootloader_task", "Format flash");
		int format_ret = m_fs.format();
		if(format_ret != SPIFFS_OK)
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
		if(mount_ret != SPIFFS_OK)
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
	init_usb();

	m_fastboot.set_download_buffer(reinterpret_cast<uint8_t*>(0x24000000), 512*1024*1024);
	m_fastboot.set_fs(&m_fs);

	std::function<bool(void)> has_line_pred   = std::bind(&USB_rx_buffer_task::has_line, &usb_rx_buffer_task);
	std::function<bool(void)> has_buffer_pred = std::bind(&USB_rx_buffer_task::has_data, &usb_rx_buffer_task);
	
	std::vector<uint8_t> in_buffer;
	in_buffer.reserve(1024);

	std::vector<uint8_t> out_buffer;
	out_buffer.reserve(1024);

	for(;;)
	{
		{
			std::unique_lock<Mutex_static> lock(usb_rx_buffer_task.get_mutex());
		
			if(m_fastboot.get_state() == Fastboot::Fastboot_state::LINE_MODE)
			{
				usb_rx_buffer_task.get_cv().wait(lock, std::cref(has_line_pred));
			}
			else if(m_fastboot.get_state() == Fastboot::Fastboot_state::PACKET_MODE)
			{
				usb_rx_buffer_task.get_cv().wait(lock, std::cref(has_buffer_pred));
			}
			else
			{
				for(;;)
				{

				}
			}

			if(m_fastboot.get_state() == Fastboot::Fastboot_state::LINE_MODE)
			{
				if(!usb_rx_buffer_task.get_line(&in_buffer))
				{
					continue;
				}
			}
			else if(m_fastboot.get_state() == Fastboot::Fastboot_state::PACKET_MODE)
			{
				if(!usb_rx_buffer_task.get_data(&in_buffer, 1024))
				{
					continue;
				}
			}
			else
			{
				for(;;)
				{

				}
			}
		}

		//either a line or a data mode packet
		m_fastboot.process(in_buffer, &out_buffer);

		if(!out_buffer.empty())
		{
			usb_tx_buffer_task.write(out_buffer.begin(), out_buffer.end());
			out_buffer.clear();
		}
	}
}

bool Bootloader_task::load_verify_hex_app_image()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	const char* fname = "app.hex";
	int fd = SPIFFS_open(m_fs.get_fs(), fname, SPIFFS_RDONLY, 0);
	if(fd < 0)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, fname, SPIFFS_errno(m_fs.get_fs()));
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

	int read_ret = 0;
	do
	{
		read_ret = SPIFFS_read(m_fs.get_fs(), fd, read_buffer.data(), read_buffer.size());
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

			if(!hex_loader.process_line(line_buffer))
			{
				logger->log(LOG_LEVEL::error, "Bootloader_task", "hex_loader.process_line failed: %s", line_buffer.c_str());
				break;
			}

		} while(line_it != file_buffer.end());
	} while(read_ret > 0);
	
	s32_t close_ret = SPIFFS_close(m_fs.get_fs(), fd);
	if(close_ret != SPIFFS_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, fname, SPIFFS_errno(m_fs.get_fs()));
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

	int fd = SPIFFS_open(m_fs.get_fs(), fname, SPIFFS_RDONLY, 0);
	if(fd < 0)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, fname, SPIFFS_errno(m_fs.get_fs()));
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

	int read_ret = 0;
	do
	{
		read_ret = SPIFFS_read(m_fs.get_fs(), fd, read_buffer.data(), read_buffer.size());
		if(read_ret < 0)
		{
			return false;
		}

		mbedtls_md5_update_ret(&md5_ctx, (unsigned char*) read_buffer.data(), read_ret);

		std::copy_n(read_buffer.data(), read_ret, axi_base + num_written);
		num_written += read_ret;

	} while(read_ret > 0);
	
	s32_t close_ret = SPIFFS_close(m_fs.get_fs(), fd);
	if(close_ret != SPIFFS_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, fname, SPIFFS_errno(m_fs.get_fs()));
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

	int fd = SPIFFS_open(m_fs.get_fs(), appfname, SPIFFS_RDONLY, 0);
	if(fd < 0)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, appfname, SPIFFS_errno(m_fs.get_fs()));
		return false;
	}

	AES_GCM_aux_data aux_data;
	{
		int fd_aux = SPIFFS_open(m_fs.get_fs(), appauxfname, SPIFFS_RDONLY, 0);
		if(fd < 0)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "Opening file %s failed: %" PRId32, appauxfname, SPIFFS_errno(m_fs.get_fs()));
			return false;
		}

		spiffs_stat app_aux_stat;
		int ret = SPIFFS_fstat(m_fs.get_fs(), fd_aux, &app_aux_stat);
		if(ret != SPIFFS_OK)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "fstat file %s failed: %" PRId32, appauxfname, SPIFFS_errno(m_fs.get_fs()));
			return false;
		}

		std::vector<char> aux_data_file(app_aux_stat.size);
		int read_ret = SPIFFS_read(m_fs.get_fs(), fd_aux, aux_data_file.data(), aux_data_file.size());
		if(read_ret < 0)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "file %s read failed: %" PRId32, appauxfname, SPIFFS_errno(m_fs.get_fs()));
			return false;
		}
		else if(size_t(read_ret) != aux_data_file.size())
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "file %s read failed: %" PRId32, appauxfname, SPIFFS_errno(m_fs.get_fs()));
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

		s32_t close_ret = SPIFFS_close(m_fs.get_fs(), fd_aux);
		if(close_ret != SPIFFS_OK)
		{
			logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, appauxfname, SPIFFS_errno(m_fs.get_fs()));
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
			read_ret = SPIFFS_read(m_fs.get_fs(), fd, in_block.data(), in_block.size());
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

	s32_t close_ret = SPIFFS_close(m_fs.get_fs(), fd);
	if(close_ret != SPIFFS_OK)
	{
		logger->log(LOG_LEVEL::error, "Bootloader_task", "close file %s failed: %" PRId32, appfname, SPIFFS_errno(m_fs.get_fs()));
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
		"dsb 0xF\n"
		"isb 0xF\n"
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
		"dsb 0xF\n"
		"isb 0xF\n"
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
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_AHB1_RELEASE_RESET();
	__HAL_RCC_AHB2_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_AHB2_RELEASE_RESET();

	//we can't bulk reset AHB3 because that resets the cpu and fmc
	// __HAL_RCC_AHB3_FORCE_RESET();
	// __HAL_RCC_AHB3_RELEASE_RESET();
	__HAL_RCC_MDMA_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_MDMA_RELEASE_RESET();
	__HAL_RCC_DMA2D_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_DMA2D_RELEASE_RESET();
	__HAL_RCC_JPGDECRST_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_JPGDECRST_RELEASE_RESET();
	// __HAL_RCC_FMC_FORCE_RESET();
	// __HAL_RCC_FMC_RELEASE_RESET();
	__HAL_RCC_QSPI_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_QSPI_RELEASE_RESET();
	__HAL_RCC_SDMMC1_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_SDMMC1_RELEASE_RESET();
	// __HAL_RCC_CPU_FORCE_RESET();
	// __HAL_RCC_CPU_RELEASE_RESET();

	__HAL_RCC_AHB4_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_AHB4_RELEASE_RESET();

	__HAL_RCC_APB1L_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB1L_RELEASE_RESET();
	__HAL_RCC_APB1H_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB1H_RELEASE_RESET();
	__HAL_RCC_APB2_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB2_RELEASE_RESET();
	__HAL_RCC_APB3_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB3_RELEASE_RESET();
	__HAL_RCC_APB4_FORCE_RESET();
	asm volatile("dsb 0xF\n" : /* no out */	: /* no in */ : "memory");
	__HAL_RCC_APB4_RELEASE_RESET();

	//Sync
	asm volatile(
		"dsb 0xF\n"
		"isb 0xF\n"
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
		"dsb 0xF\n"
		"isb 0xF\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	__set_CONTROL(0);
	__set_MSP(estack);

	asm volatile(
		"dsb 0xF\n"
		"isb 0xF\n"
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

	FLASH_OBProgramInitTypeDef ob_init;
	ob_init.Banks = FLASH_BANK_1;
	HAL_FLASHEx_OBGetConfig(&ob_init);

	bool ret = true;
	// if(ob_init.WRPState != OB_WRPSTATE_ENABLE)
	// {
	// 	logger->log(LOG_LEVEL::fatal, "Bootloader_task", "WRPState incorrect");
	// 	ret = false;
	// }
	// if(ob_init.WRPSector != OB_WRP_SECTOR_0)
	// {
	// 	logger->log(LOG_LEVEL::fatal, "Bootloader_task", "WRPSector incorrect");
	// 	ret = false;
	// }
	if(ob_init.RDPLevel != OB_RDP_LEVEL_1)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "RDPLevel incorrect");
		ret = false;
	}
	if(ob_init.BORLevel != OB_BOR_LEVEL3)
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "BORLevel incorrect");
		ret = false;
	}

	return ret;
}

bool Bootloader_task::config_option_bytes()
{
	// HAL_FLASH_Unlock();

	HAL_FLASH_OB_Unlock();

	FLASH_OBProgramInitTypeDef ob_init;
	ob_init.Banks = FLASH_BANK_1;
	HAL_FLASHEx_OBGetConfig(&ob_init);

#if 1
	ob_init.OptionType = OPTIONBYTE_BOR | OPTIONBYTE_RDP;
	ob_init.WRPState = OB_WRPSTATE_DISABLE;
	ob_init.WRPSector = OB_WRP_SECTOR_All;
	ob_init.RDPLevel = OB_RDP_LEVEL_1;
	ob_init.BORLevel = OB_BOR_LEVEL3;
	// ob_init.USERType
	// ob_init.USERConfig
	ob_init.Banks = FLASH_BANK_1;
	// ob_init.PCROPConfig
	// ob_init.PCROPStartAddr
	// ob_init.PCROPEndAddr
	// ob_init.BootConfig
	// ob_init.BootAddr0
	// ob_init.BootAddr1
	// ob_init.SecureAreaConfig
	// ob_init.SecureAreaStartAddr
	// ob_init.SecureAreaEndAddr
#else

	ob_init.OptionType = OPTIONBYTE_BOR | OPTIONBYTE_RDP | OPTIONBYTE_WRP;
	ob_init.WRPState = OB_WRPSTATE_ENABLE;
	ob_init.WRPSector = OB_WRP_SECTOR_0;
	ob_init.RDPLevel = OB_RDP_LEVEL_1;
	ob_init.BORLevel = OB_BOR_LEVEL3;
	// ob_init.USERType
	// ob_init.USERConfig
	ob_init.Banks = FLASH_BANK_1;
	// ob_init.PCROPConfig
	// ob_init.PCROPStartAddr
	// ob_init.PCROPEndAddr
	// ob_init.BootConfig
	// ob_init.BootAddr0
	// ob_init.BootAddr1
	// ob_init.SecureAreaConfig
	// ob_init.SecureAreaStartAddr
	// ob_init.SecureAreaEndAddr
#endif
	HAL_StatusTypeDef prog_ret = HAL_FLASHEx_OBProgram(&ob_init);
	if(prog_ret != HAL_OK)
	{
		return false;
	}

	HAL_StatusTypeDef launch_ret = HAL_FLASH_OB_Launch();
	if(launch_ret != HAL_OK)
	{
		return false;
	}

	return true;
}


bool Bootloader_task::handle_usb_set_config_thunk(void* ctx, const uint16_t config)
{
	return static_cast<Bootloader_task*>(ctx)->handle_usb_set_config(config);
}

bool Bootloader_task::init_usb()
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	//set id
	get_unique_id_str(&usb_id_str);

	logger->log(LOG_LEVEL::info, "main", "usb_driver.set_ep0_buffer");
	usb_driver.set_ep0_buffer(&usb_ep0_buffer);

	logger->log(LOG_LEVEL::info, "main", "usb_driver.set_tx_buffer");
	usb_driver.set_tx_buffer(&usb_tx_buffer);

	logger->log(LOG_LEVEL::info, "main", "usb_driver.set_rx_buffer");
	usb_driver.set_rx_buffer(&usb_rx_buffer);
	
	logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_driver.initialize");
	if(!usb_driver.initialize())
	{
		logger->log(LOG_LEVEL::fatal, "Bootloader_task", "usb_driver.initialize failed");
		return false;	
	}

	logger->log(LOG_LEVEL::info, "Bootloader_task", "Generate usb descriptor");
	//lifetime mgmt of some of these is broken
	{
		Device_descriptor dev_desc;
		dev_desc.bcdUSB = USB_common::build_bcd(2, 0, 0);
		// dev_desc.bDeviceClass    = static_cast<uint8_t>(USB_common::CLASS_DEF::CLASS_PER_INTERFACE);
		// dev_desc.bDeviceSubClass = static_cast<uint8_t>(USB_common::SUBCLASS_DEF::SUBCLASS_NONE);
		dev_desc.bDeviceClass    = 0x02;
		dev_desc.bDeviceSubClass = 0x02;
		dev_desc.bDeviceProtocol = static_cast<uint8_t>(USB_common::PROTO_DEF::PROTO_NONE);
		// dev_desc.bMaxPacketSize0 = m_driver->get_ep0_config().size;
		dev_desc.bMaxPacketSize0 = 64;
		dev_desc.idVendor  = 0x0483;
		dev_desc.idProduct = 0x5740;
		dev_desc.bcdDevice = USB_common::build_bcd(1, 0, 0);
		dev_desc.iManufacturer      = 1;
		dev_desc.iProduct           = 2;
		dev_desc.iSerialNumber      = 3;
		dev_desc.bNumConfigurations = 1;

		usb_desc_table.set_device_descriptor(dev_desc, 0);
	}
	{
		//9 byte ea
		Interface_descriptor desc;
		desc.bInterfaceNumber   = 0;
		desc.bAlternateSetting  = 0;
		desc.bNumEndpoints      = 1;
		desc.bInterfaceClass    = static_cast<uint8_t>(CDC::COMM_INTERFACE_CLASS_CODE);
		desc.bInterfaceSubClass = static_cast<uint8_t>(CDC::COMM_INTERFACE_SUBCLASS_CODE::ACM);
		desc.bInterfaceProtocol = static_cast<uint8_t>(CDC::COMM_CLASS_PROTO_CODE::V250);
		// desc.bInterfaceProtocol = static_cast<uint8_t>(CDC::COMM_CLASS_PROTO_CODE::NONE);
		desc.iInterface         = 5;
		usb_desc_table.set_interface_descriptor(desc, 0);
	}
	{
		Interface_descriptor desc;
		desc.bInterfaceNumber   = 1;
		desc.bAlternateSetting  = 0;
		desc.bNumEndpoints      = 2;
		desc.bInterfaceClass    = static_cast<uint8_t>(CDC::DATA_INTERFACE_CLASS_CODE);
		desc.bInterfaceSubClass = static_cast<uint8_t>(CDC::DATA_INTERFACE_SUBCLASS_CODE);
		desc.bInterfaceProtocol = static_cast<uint8_t>(CDC::DATA_INTERFACE_PROTO_CODE::NONE);
		desc.iInterface         = 6;
		usb_desc_table.set_interface_descriptor(desc, 1);
	}
	{
		//7 byte ea
		Endpoint_descriptor desc;
		desc.bEndpointAddress = 0x00 | 0x01;
		desc.bmAttributes     = static_cast<uint8_t>(Endpoint_descriptor::ATTRIBUTE_TRANSFER::BULK);
		desc.wMaxPacketSize   = 512;
		desc.bInterval        = 0;
		usb_desc_table.set_endpoint_descriptor(desc, desc.bEndpointAddress);

		desc.bEndpointAddress = 0x80 | 0x01;
		desc.bmAttributes     = static_cast<uint8_t>(Endpoint_descriptor::ATTRIBUTE_TRANSFER::BULK);
		desc.wMaxPacketSize   = 512;
		desc.bInterval        = 0;
		usb_desc_table.set_endpoint_descriptor(desc, desc.bEndpointAddress);

		desc.bEndpointAddress = 0x80 | 0x02;
		// desc.bmAttributes     = static_cast<uint8_t>(Endpoint_descriptor::ATTRIBUTE_TRANSFER::BULK);
		desc.bmAttributes     = static_cast<uint8_t>(Endpoint_descriptor::ATTRIBUTE_TRANSFER::INTERRUPT);
		desc.wMaxPacketSize   = 8;
		desc.bInterval        = 8;//for HS, period is 2^(bInterval-1) * 125 us, so 8 -> 16ms
		usb_desc_table.set_endpoint_descriptor(desc, desc.bEndpointAddress);
	}

	{
		//configuration 1
		Config_desc_table::Config_desc_ptr desc_ptr = std::make_shared<Configuration_descriptor>();
		desc_ptr->wTotalLength = 0;//updated later
		desc_ptr->bNumInterfaces = 2;
		desc_ptr->bConfigurationValue = 1;
		desc_ptr->iConfiguration = 4;
		desc_ptr->bmAttributes = static_cast<uint8_t>(Configuration_descriptor::ATTRIBUTES::NONE);
		desc_ptr->bMaxPower = Configuration_descriptor::ma_to_maxpower(150);

		usb_desc_table.set_config_descriptor(desc_ptr, 0);
	}
	{
		std::shared_ptr<String_descriptor_zero> desc_ptr = std::make_shared<String_descriptor_zero>();

		const static String_descriptor_zero::LANGID lang[] = {String_descriptor_zero::LANGID::ENUS};
		desc_ptr->assign(lang, 1);

		usb_desc_table.set_string_descriptor(desc_ptr, String_descriptor_zero::LANGID::NONE, 0);
	}
	{
		String_descriptor_base desc;
		desc.assign("Suburban Marine, Inc.");
		usb_desc_table.set_string_descriptor(desc, String_descriptor_zero::LANGID::ENUS, 1);
	}
	{
		String_descriptor_base desc;
		desc.assign("SM-1301 Bootloader");
		usb_desc_table.set_string_descriptor(desc, String_descriptor_zero::LANGID::ENUS, 2);
	}
	{
		String_descriptor_base desc;
		desc.assign(usb_id_str.data());
		usb_desc_table.set_string_descriptor(desc, String_descriptor_zero::LANGID::ENUS, 3);
	}
	{
		String_descriptor_base desc;
		desc.assign("Default configuration");
		usb_desc_table.set_string_descriptor(desc, String_descriptor_zero::LANGID::ENUS, 4);
	}
	{
		String_descriptor_base desc;
		desc.assign("Communications");
		usb_desc_table.set_string_descriptor(desc, String_descriptor_zero::LANGID::ENUS, 5);
	}
	{
		String_descriptor_base desc;
		desc.assign("CDC Data");
		usb_desc_table.set_string_descriptor(desc, String_descriptor_zero::LANGID::ENUS, 6);
	}
	std::shared_ptr<CDC::CDC_header_descriptor> cdc_header_desc = std::make_shared<CDC::CDC_header_descriptor>();
	cdc_header_desc->bcdCDC = USB_common::build_bcd(1,1,0);
	usb_desc_table.add_other_descriptor(cdc_header_desc);

	std::shared_ptr<CDC::CDC_call_management_descriptor> cdc_call_mgmt_desc = std::make_shared<CDC::CDC_call_management_descriptor>();
	cdc_call_mgmt_desc->set_self_call_mgmt_handle(false);
	cdc_call_mgmt_desc->bDataInterface = 1;
	usb_desc_table.add_other_descriptor(cdc_call_mgmt_desc);

	std::shared_ptr<CDC::CDC_acm_descriptor> cdc_acm_desc = std::make_shared<CDC::CDC_acm_descriptor>();
	// cdc_acm_desc->bmCapabilities = 0x00;
	cdc_acm_desc->set_support_network_connection(true);
	cdc_acm_desc->set_support_send_break(false);
	cdc_acm_desc->set_support_line(true);
	cdc_acm_desc->set_support_comm(true);
	// cdc_acm_desc->bmCapabilities = 0x03;
	usb_desc_table.add_other_descriptor(cdc_acm_desc);

	std::shared_ptr<CDC::CDC_union_descriptor> cdc_union_desc = std::make_shared<CDC::CDC_union_descriptor>();
	cdc_union_desc->bMasterInterface = 0;
	cdc_union_desc->bSlaveInterface0 = 1;
	usb_desc_table.add_other_descriptor(cdc_union_desc);
	
	Config_desc_table::Config_desc_ptr config_desc_ptr = usb_desc_table.get_config_descriptor(0);

	//register iface and ep to configuration
	config_desc_ptr->get_desc_list().push_back( usb_desc_table.get_interface_descriptor(0).get() );
	config_desc_ptr->get_desc_list().push_back( cdc_header_desc.get() );
	config_desc_ptr->get_desc_list().push_back( cdc_acm_desc.get() );
	config_desc_ptr->get_desc_list().push_back( cdc_union_desc.get() );
	config_desc_ptr->get_desc_list().push_back( cdc_call_mgmt_desc.get() );
	config_desc_ptr->get_desc_list().push_back( usb_desc_table.get_endpoint_descriptor(0x82).get() );

	config_desc_ptr->get_desc_list().push_back( usb_desc_table.get_interface_descriptor(1).get() );
	config_desc_ptr->get_desc_list().push_back( usb_desc_table.get_endpoint_descriptor(0x01).get() );
	config_desc_ptr->get_desc_list().push_back( usb_desc_table.get_endpoint_descriptor(0x81).get() );

	config_desc_ptr->wTotalLength = config_desc_ptr->get_total_size();

	logger->log(LOG_LEVEL::info, "Bootloader_task", "Allocate buffers");

	m_rx_buf.resize(1024);
	m_rx_buf_adapter;
	m_rx_buf_adapter.reset(m_rx_buf.data(), m_rx_buf.size());

	m_tx_buf.resize(1024);
	m_tx_buf_adapter;
	m_tx_buf_adapter.reset(m_tx_buf.data(), m_tx_buf.size());

	logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_core.initialize");
	if(!usb_core.initialize(&usb_driver, 8, m_tx_buf_adapter, m_rx_buf_adapter))
	{
		logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_core.initialize failed");
		return false;
	}

	logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_core set descriptors");
	usb_core.set_usb_class(&usb_cdc);
	usb_core.set_descriptor_table(&usb_desc_table);
	usb_core.set_config_callback(&handle_usb_set_config_thunk, this);

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

	// HAL_NVIC_SetPriority(OTG_HS_IRQn, 5, 0);
	// HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

	// usb_core.set_control_callback(std::bind(&Main_task::usb_control_callback, this, std::placeholders::_1));
	// usb_core.set_config_callback(std::bind(&Main_task::usb_config_callback, this));
	// usb_core.set_descriptor_callback(std::bind(&Main_task::usb_get_descriptor_callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_core.enable");
	if(!usb_core.enable())
	{
		logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_core.enable failed");
		return false;
	}



	//process usb packets
	usb_core_task.set_usb_core(&usb_core);
	usb_core_task.launch("usb_core", 1);

	usb_cdc_task.set_usb_core(&usb_core);
	usb_cdc_task.launch("usb_cdc", 1);

	usb_drvr_task.set_usb_core(&usb_core);
	usb_drvr_task.launch("usb_drvr", 1);

	usb_rx_buffer_task.set_usb_driver(&usb_driver);
	usb_rx_buffer_task.launch("usb_rx_buf", 4);

	usb_tx_buffer_task.set_usb_driver(&usb_driver);
	usb_tx_buffer_task.launch("usb_tx_buf", 5);

	logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_core.connect");
	if(!usb_core.connect())
	{
		logger->log(LOG_LEVEL::info, "Bootloader_task", "usb_core.connect failed");
		return false;
	}

	return true;
}

bool Bootloader_task::handle_usb_set_config(const uint8_t config)
{
	bool ret = false;

	switch(config)
	{
		case 0:
		{
			usb_core.get_driver()->ep_stall(0x01);
			usb_core.get_driver()->ep_stall(0x81);
			usb_core.get_driver()->ep_stall(0x82);
			ret = true;
			break;
		}
		case 1:
		{


			//out 1
			{
				Endpoint_desc_table::Endpoint_desc_const_ptr ep_data_out = usb_desc_table.get_endpoint_descriptor(0x01);

				usb_driver_base::ep_cfg ep1;
				ep1.num  = ep_data_out->bEndpointAddress;
				ep1.size = ep_data_out->wMaxPacketSize;
				ep1.type = usb_driver_base::EP_TYPE::BULK;
				usb_core.get_driver()->ep_config(ep1);
			}
			//in 1
			{
				Endpoint_desc_table::Endpoint_desc_const_ptr ep_data_in = usb_desc_table.get_endpoint_descriptor(0x81);
				usb_driver_base::ep_cfg ep2;
				ep2.num  = ep_data_in->bEndpointAddress;
				ep2.size = ep_data_in->wMaxPacketSize;
				ep2.type = usb_driver_base::EP_TYPE::BULK;
				usb_core.get_driver()->ep_config(ep2);
			}
			//in 2
			{
				Endpoint_desc_table::Endpoint_desc_const_ptr ep_notify_in = usb_desc_table.get_endpoint_descriptor(0x82);

				usb_driver_base::ep_cfg ep3;
				ep3.num  = ep_notify_in->bEndpointAddress;
				ep3.size = ep_notify_in->wMaxPacketSize;
				ep3.type = usb_driver_base::EP_TYPE::INTERRUPT;
				usb_core.get_driver()->ep_config(ep3);
			}

			usb_cdc_task.notify_new_connection();

			ret = true;
			break;
		}
		default:
		{
			ret = false;
			break;
		}
	}

	return ret;
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
