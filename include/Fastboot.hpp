#pragma once

#include "spiffs_int.hpp"

#include <array>
#include <vector>

class Fastboot
{
public:

	Fastboot()
	{
		m_state = Fastboot_state::LINE_MODE;

		m_addr = nullptr;
		m_max_size = 0;

		m_size = 0;
		m_curr_off = 0;

		m_fs = nullptr;
	}

	void process(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);

	enum class Fastboot_state
	{
		LINE_MODE,
		PACKET_MODE
	};

	Fastboot_state get_state()
	{
		return m_state;
	}

	void set_fs(SPIFFS_int* const fs)
	{
		m_fs = fs;
	}

	void set_download_buffer(uint8_t* const addr, const size_t max_size)
	{
		m_addr = addr;
		m_max_size = max_size;
	}

protected:

	void set_state(const Fastboot_state& state)
	{
		m_state = state;
	}

	void handle_getvar(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_download(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_verify(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_flash(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_erase(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_format(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_boot(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_continue(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_reboot(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_reboot_bootloader(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);
	void handle_powerdown(const std::vector<uint8_t>& in_packet, std::vector<uint8_t>* resp);

	Fastboot_state m_state;

	//temp buffer space
	volatile uint8_t* m_addr;
	size_t m_max_size;

	//info set by download and flash
	size_t m_size;

	//download state
	size_t m_curr_off;

	//use this fs
	SPIFFS_int* m_fs;

	constexpr static std::array<char, 4> RESP_OKAY = {'O', 'K', 'A', 'Y'};
	constexpr static std::array<char, 4> RESP_FAIL = {'F', 'A', 'I', 'L'};
	constexpr static std::array<char, 4> RESP_DATA = {'D', 'A', 'T', 'A'};
	constexpr static std::array<char, 4> RESP_INFO = {'I', 'N', 'F', 'O'};

	constexpr static std::array<char, 6> CMD_getvar 				= {'g', 'e', 't', 'v', 'a', 'r'};
	constexpr static std::array<char, 8> CMD_download 				= {'d', 'o', 'w', 'n', 'l', 'o', 'a', 'd'};
	constexpr static std::array<char, 6> CMD_verify 				= {'v', 'e', 'r', 'i', 'f', 'y'};
	constexpr static std::array<char, 5> CMD_flash 					= {'f', 'l', 'a', 's', 'h'};
	constexpr static std::array<char, 5> CMD_erase 					= {'e', 'r', 'a', 's', 'e'};
	constexpr static std::array<char, 6> CMD_format 				= {'f', 'o', 'r', 'm', 'a', 't'};
	constexpr static std::array<char, 4> CMD_boot 					= {'b', 'o', 'o', 't',};
	constexpr static std::array<char, 8> CMD_continue 				= {'c', 'o', 'n', 't', 'i', 'n', 'u', 'e'};
	constexpr static std::array<char, 6> CMD_reboot 				= {'r', 'e', 'b', 'o', 'o', 't'};
	constexpr static std::array<char, 17> CMD_reboot_bootloader 	= {'r', 'e', 'b', 'o', 'o', 't', '_', 'b', 'o', 'o', 't', 'l', 'o', 'a', 'd', 'e', 'r'};
	constexpr static std::array<char, 9> CMD_powerdown 				= {'p', 'o', 'w', 'e', 'r', 'd', 'o', 'w', 'n'};

	template <size_t LEN>
	bool command_match(const std::vector<uint8_t>& in_packet, const std::array<char, LEN>& cmd)
	{
		if(in_packet.size() <= cmd.size())
		{
			return false;
		}

		return std::equal(cmd.begin(), cmd.end(), in_packet.begin());
	}
};