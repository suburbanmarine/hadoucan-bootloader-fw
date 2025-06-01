#pragma once

#include "W25Q16JV.hpp"
#include "W25Q16JV_app_region.hpp"

#include "crc/crc_32c.hpp"

#include "bootloader_util/Bootloader_key.hpp"

#include "Fastboot.hpp"

#include "freertos_cpp_util/Task_static.hpp"

#include "tinyxml2/tinyxml2.h"

class Bootloader_task : public Task_static<2048>
{
public:

	Bootloader_task()
	{

	}

	void work() override;

	Bootloader_key clear_bootloader_key()
	{
		return set_bootloader_key(Bootloader_key::Bootloader_ops::RUN_APP);
	} 
	
	Bootloader_key set_bootloader_key(const Bootloader_key::Bootloader_ops op)
	{
		//BBRAM, 0x38800000, 4K
		Bootloader_key key;
		key.update_magic_sig();
		key.bootloader_op = static_cast<uint8_t>(op);
		key.update_crc();

		key.to_addr(reinterpret_cast<uint8_t*>(0x38800000));
		asm volatile(
			"dsb 0xF\n"
			"isb 0xF\n"
			: /* no out */
			: /* no in */
			: "memory"
			);

		return key;
	} 
	
	static void get_unique_id(std::array<uint32_t, 3>* const id);
	static void get_unique_id_str(std::array<char, 25>* const id_str);

protected:

	bool init_usb();

	void jump_to_addr(uint32_t estack, uint32_t jump_addr) __attribute__((noreturn));

	bool load_verify_hex_app_image();
	bool load_verify_bin_app_image();
	bool load_verify_bin_gcm_app_image();


	W25Q16JV m_qspi;
	W25Q16JV_app_region m_fs;

	std::array<char, 25> usb_id_str;

	Fastboot m_fastboot;

	bool check_option_bytes();
	bool config_option_bytes();
};
