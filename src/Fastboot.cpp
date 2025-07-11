#include "Fastboot.hpp"

#include "freertos_cpp_util/logging/Global_logger.hpp"

#include "bootloader_util/Bootloader_key.hpp"

#include "stm32h7xx_hal.h"

#include <algorithm>
#include <cinttypes>

using freertos_util::logging::LOG_LEVEL;

constexpr std::array<char, 4> Fastboot::RESP_OKAY;
constexpr std::array<char, 4> Fastboot::RESP_FAIL;
constexpr std::array<char, 4> Fastboot::RESP_DATA;
constexpr std::array<char, 4> Fastboot::RESP_INFO;

constexpr std::array<char, 6> Fastboot::CMD_getvar;
constexpr std::array<char, 8> Fastboot::CMD_download;
constexpr std::array<char, 6> Fastboot::CMD_verify;
constexpr std::array<char, 5> Fastboot::CMD_flash;
constexpr std::array<char, 5> Fastboot::CMD_erase;
constexpr std::array<char, 6> Fastboot::CMD_format;
constexpr std::array<char, 4> Fastboot::CMD_boot;
constexpr std::array<char, 8> Fastboot::CMD_continue;
constexpr std::array<char, 6> Fastboot::CMD_reboot;
constexpr std::array<char, 17> Fastboot::CMD_reboot_bootloader;
constexpr std::array<char, 9> Fastboot::CMD_powerdown;

void Fastboot::process(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	if(get_state() == Fastboot_state::PACKET_MODE)
	{
		handle_download(in_packet, resp);
	}
	else if(get_state() == Fastboot_state::LINE_MODE)
	{
		if(command_match(in_packet, CMD_getvar))
		{
			handle_getvar(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_download))
		{
			handle_download(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_verify))
		{
			handle_verify(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_flash))
		{
			handle_flash(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_erase))
		{
			handle_erase(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_format))
		{
			handle_format(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_boot))
		{
			handle_boot(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_continue))
		{
			handle_continue(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_reboot))
		{
			handle_reboot(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_reboot_bootloader))
		{
			handle_reboot_bootloader(in_packet, resp);
		}
		else if(command_match(in_packet, CMD_powerdown))
		{
			handle_powerdown(in_packet, resp);
		}
		else
		{
			logger->log(LOG_LEVEL::error, "Fastboot", "Unknown command %.*s", in_packet.size(), in_packet.data());
			resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());		
		}
	}
}

void Fastboot::handle_getvar(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
}
void Fastboot::handle_download(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	resp->clear();

	if(get_state() == Fastboot_state::LINE_MODE)
	{
		Stack_string<128> digit_str;
		// std::string digit_str;
		//digit_str.append("0x");

		auto it = std::find(in_packet.begin(), in_packet.end(), ':');
		if(it == in_packet.end())
		{
			logger->log(LOG_LEVEL::error, "Fastboot", "handle_download fail, no :");
			resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
			return;
		}

		it = std::next(it);
		if(std::distance(it, in_packet.end()) > 8)
		{
			digit_str.append(it, std::next(it, 8));
		}
		else
		{
			digit_str.append(it, in_packet.end());
		}

		unsigned size_in = 0;
		if(sscanf(digit_str.c_str(), "%x", &size_in) != 1)
		{
			logger->log(LOG_LEVEL::error, "Fastboot", "handle_download fail, sscanf fail");
			resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
			return;	
		}

		m_size = size_in;
		m_curr_off = 0;

		if(size_in <= m_max_size)
		{
			std::array<char, 9> hex_str;
			int ret = snprintf(hex_str.data(), hex_str.size(), "%08x", size_in);
			if(ret < 0)
			{
				resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());	
			}
			else if(size_t(ret) <= hex_str.size())
			{
				set_state(Fastboot_state::PACKET_MODE);

				resp->clear();
				resp->reserve(12);

				resp->push_back('D');
				resp->push_back('A');
				resp->push_back('T');
				resp->push_back('A');

				logger->log(LOG_LEVEL::debug, "Fastboot", "Download start: %" PRId32, uint32_t(size_in));
				resp->insert(resp->end(), hex_str.begin(), std::prev(hex_str.end()));
			}
			else
			{
				logger->log(LOG_LEVEL::error, "Fastboot", "handle_download fail, snprintf fail");
				resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
			}			
		}
	}
	else if(get_state() == Fastboot_state::PACKET_MODE)
	{
		const size_t num_to_copy = std::min(in_packet.size(), m_size - m_curr_off);

		std::copy_n(in_packet.data(), num_to_copy, m_addr + m_curr_off);
		m_curr_off += num_to_copy;

		if(m_curr_off == m_size)
		{
			logger->log(LOG_LEVEL::debug, "Fastboot", "Download complete");

			set_state(Fastboot_state::LINE_MODE);
			resp->assign(RESP_OKAY.begin(), RESP_OKAY.end());
		}
	}
	else
	{
		logger->log(LOG_LEVEL::error, "Fastboot", "handle_download fail, state fail");
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());		
	}
}
void Fastboot::handle_verify(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();
	logger->log(LOG_LEVEL::info, "Fastboot", "handle_verify");
	resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
}
void Fastboot::handle_flash(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	logger->log(LOG_LEVEL::info, "Fastboot", "handle_flash");

	resp->clear();

	auto it = std::find(in_packet.begin(), in_packet.end(), ':');
	if(it == in_packet.end())
	{
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	Stack_string<128> flash_name;

	it = std::next(it);
	for(; it != in_packet.end(); ++it)
	{
		const char c = *it;
		if(std::isalnum(c) || (c == '.'))
		{
			flash_name.push_back(c);
		}

		if(flash_name.size() >= 16)
		{
			break;
		}
	}

	logger->log(LOG_LEVEL::info, "Fastboot", "Starting to write file \"%s\"", flash_name.c_str());

	spiffs_file fd = SPIFFS_open(m_fs->get_fs(), flash_name.c_str(), SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR, 0);
	if(fd < 0)
	{
		logger->log(LOG_LEVEL::error, "Fastboot", "Opening file \"%s\" failed: %" PRId32, flash_name.c_str(), SPIFFS_errno(m_fs->get_fs()));
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	if(SPIFFS_write(m_fs->get_fs(), fd, (u8_t *)m_addr, m_size) < 0)
	{
		logger->log(LOG_LEVEL::error, "Fastboot", "Writing file \"%s\" failed: %" PRId32, flash_name.c_str(), SPIFFS_errno(m_fs->get_fs()));
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	if(SPIFFS_close(m_fs->get_fs(), fd) < 0)
	{
		logger->log(LOG_LEVEL::error, "Fastboot", "Closing file \"%s\" failed: %" PRId32, flash_name.c_str(), SPIFFS_errno(m_fs->get_fs()));
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	logger->log(LOG_LEVEL::info, "Fastboot", "Wrote \"%s\" OK", flash_name.c_str());

	resp->assign(RESP_OKAY.begin(), RESP_OKAY.end());
}
void Fastboot::handle_erase(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	logger->log(LOG_LEVEL::info, "Fastboot", "handle_erase");

	resp->clear();

	auto it = std::find(in_packet.begin(), in_packet.end(), ':');
	if(it == in_packet.end())
	{
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	Stack_string<128> flash_name;

	it = std::next(it);
	for(; it != in_packet.end(); ++it)
	{
		const char c = *it;
		if(std::isalnum(c) || (c == '.'))
		{
			flash_name.push_back(c);
		}

		if(flash_name.size() >= 16)
		{
			break;
		}
	}

	logger->log(LOG_LEVEL::info, "Fastboot", "Starting to remove file %s", flash_name.c_str());

	if(SPIFFS_remove(m_fs->get_fs(), flash_name.c_str()) < 0)
	{
		logger->log(LOG_LEVEL::error, "Fastboot", "Remove file %s failed: %" PRId32, flash_name.c_str(), SPIFFS_errno(m_fs->get_fs()));
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	logger->log(LOG_LEVEL::info, "Fastboot", "Erase %s OK", flash_name.c_str());

	resp->assign(RESP_OKAY.begin(), RESP_OKAY.end());
}
void Fastboot::handle_format(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	logger->log(LOG_LEVEL::info, "Fastboot", "handle_format");

	logger->log(LOG_LEVEL::info, "Fastboot", "Unmount flash fs");
	m_fs->unmount();

	logger->log(LOG_LEVEL::info, "Fastboot", "Start format flash fs");
	int format_ret = m_fs->format();
	if(format_ret != SPIFFS_OK)
	{
		logger->log(LOG_LEVEL::error, "Fastboot", "SPIFFS format failed: %d", format_ret);
		if(format_ret == -1)
		{
			logger->log(LOG_LEVEL::error, "Fastboot", "SPIFFS errno: %d", SPIFFS_errno(m_fs->get_fs()));
		}
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	logger->log(LOG_LEVEL::info, "Fastboot", "Re-mounting flash fs");
	int mount_ret = m_fs->mount();
	if(mount_ret != SPIFFS_OK)
	{
		logger->log(LOG_LEVEL::error, "Fastboot", "Re-mounting failed: %d", mount_ret);
		resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
		return;
	}

	logger->log(LOG_LEVEL::info, "Fastboot", "Re-mounting flash fs ok");

	resp->assign(RESP_OKAY.begin(), RESP_OKAY.end());
}
void Fastboot::handle_boot(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	logger->log(LOG_LEVEL::info, "Fastboot", "handle_boot");

	handle_reboot(in_packet, resp);
}
void Fastboot::handle_continue(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	logger->log(LOG_LEVEL::info, "Fastboot", "handle_continue");

	handle_reboot(in_packet, resp);
}
void Fastboot::handle_reboot(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	logger->log(LOG_LEVEL::info, "Fastboot", "handle_reboot");

	resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());

	Bootloader_key key;
	key.update_magic_sig();
	key.bootloader_op = static_cast<uint8_t>(Bootloader_key::Bootloader_ops::RUN_APP);
	key.update_crc();

	key.to_addr(reinterpret_cast<uint8_t*>(0x38800000));

	//Disable ISR, sync
	asm volatile(
		"cpsid i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	//reboot
	NVIC_SystemReset();

	for(;;)
	{

	}
}
void Fastboot::handle_reboot_bootloader(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	logger->log(LOG_LEVEL::info, "Fastboot", "handle_reboot_bootloader");

	resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());

	Bootloader_key key;
	key.update_magic_sig();
	key.bootloader_op = static_cast<uint8_t>(Bootloader_key::Bootloader_ops::RUN_BOOTLDR);
	key.update_crc();

	key.to_addr(reinterpret_cast<uint8_t*>(0x38800000));

	//Disable ISR, sync
	asm volatile(
		"cpsid i\n"
		"isb sy\n"
		"dsb sy\n"
		: /* no out */
		: /* no in */
		: "memory"
		);

	//reboot
	NVIC_SystemReset();

	for(;;)
	{
		
	}
}
void Fastboot::handle_powerdown(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp)
{
	freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();
	
	logger->log(LOG_LEVEL::info, "Fastboot", "handle_powerdown");

	resp->assign(RESP_FAIL.begin(), RESP_FAIL.end());
}