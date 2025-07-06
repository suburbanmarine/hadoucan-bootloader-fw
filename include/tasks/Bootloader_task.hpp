#pragma once

#include "W25Q16JV.hpp"
#include "W25Q16JV_app_region.hpp"

#include "bootloader_util/Bootloader_key.hpp"

#include "freertos_cpp_util/Task_static.hpp"

#include <mbedtls/md5.h>

#include "tinyxml2/tinyxml2.h"

#include "tusb.h"

#include <memory>

class LFS_file
{
public:
	LFS_file(LFS_int* const fs)
	{
		m_fs = fs;
	}
	virtual ~LFS_file()
	{
		if(m_fd)
		{
			if(lfs_file_close(m_fs->get_fs(), m_fd.get()) < 0)
			{
				// Log?
			}
		}

		m_fd.reset();
	}

	lfs_t* get_fs()
	{
		return m_fs->get_fs();
	}

	lfs_file_t* get_fd()
	{
		return m_fd.get();
	}

	int open(const char* path, int flags)
	{
		if(m_fd)
		{
			return -1;
		}

		m_fd = std::make_unique<lfs_file_t>();
		if( ! m_fd )
		{
			return -1;
		}
		*m_fd = {};

		int ret = lfs_file_open(get_fs(), get_fd(), path, flags);
		if(ret < 0)
		{
			m_fd.reset();
		}

		return ret;
	}

	int close()
	{
		int ret = lfs_file_close(get_fs(), get_fd());
		m_fd.reset();
		return ret;
	}

private:
	LFS_file(const LFS_file& rhs) = delete;
	LFS_file& operator=(const LFS_file& rhs) = delete;

	LFS_int* m_fs;
	std::unique_ptr<lfs_file_t> m_fd;
};

class Bootloader_task : public Task_static<2048>
{
public:

	Bootloader_task()
	{

	}

	void work() override;

	static Bootloader_key clear_bootloader_key()
	{
		return set_bootloader_key(Bootloader_key::Bootloader_ops::LOAD_APP);
	} 
	
	static Bootloader_key set_bootloader_key(const Bootloader_key::Bootloader_ops op)
	{
		//BBRAM, 0x38800000, 4K
		Bootloader_key key(op);
		
		asm volatile(
			"cpsid i\n"
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		HAL_PWR_EnableBkUpAccess();

		asm volatile(
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		key.to_addr(reinterpret_cast<uint8_t*>(0x38800000));

		asm volatile(
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		HAL_PWR_DisableBkUpAccess();

		asm volatile(
			"cpsie i\n"
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		return key;
	}

	static Bootloader_key set_bootloader_key(const Bootloader_key::Bootloader_ops op, const std::array<uint8_t, 16>& app_md5, const uint32_t length)
	{
		//BBRAM, 0x38800000, 4K
		Bootloader_key key(op, app_md5, length);

		asm volatile(
			"cpsid i\n"
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		HAL_PWR_EnableBkUpAccess();

		asm volatile(
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		key.to_addr(reinterpret_cast<uint8_t*>(0x38800000));

		asm volatile(
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		HAL_PWR_DisableBkUpAccess();

		asm volatile(
			"cpsie i\n"
			"isb sy\n"
			"dsb sy\n"
			: /* no out */
			: /* no in */
			: "memory"
		);

		return key;
	}
	
	static void get_unique_id(std::array<uint32_t, 3>* const id);
	static void get_unique_id_str(std::array<char, 25>* const id_str);

	// called from usb thread
	uint32_t handle_tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state);
	void handle_tud_dfu_download_cb (uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length);
	void handle_tud_dfu_manifest_cb(uint8_t alt);
	uint16_t handle_tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length);
	void handle_tud_dfu_detach_cb(void);
	void handle_tud_dfu_abort_cb(uint8_t alt);

	static void jump_to_addr(uint32_t estack, uint32_t jump_addr) __attribute__((noreturn));

	static void zero_axi_sram();

	static void ecc_flush_axi_sram(const uint32_t length_bytes);
	static void ecc_flush_bbram(const uint32_t length_bytes);

	static std::array<uint8_t, 16> calculate_md5_axi_sram(const uint32_t length);

protected:

	bool init_usb();

	bool load_verify_bin_app_image(Bootloader_key* const key);
	bool load_verify_bin_gcm_app_image();

	void sync_and_reset();
	W25Q16JV m_qspi;
	W25Q16JV_app_region m_fs;

	std::array<char, 25> usb_id_str;

	std::shared_ptr<LFS_file> m_fd;
	mbedtls_md5_context m_fd_md5_ctx;

	static uint8_t* const m_mem_base;
	static constexpr size_t m_mem_size            = 512*1024;
	static constexpr size_t m_download_block_size = CFG_TUD_DFU_XFER_BUFSIZE;

	bool check_option_bytes();
	bool config_option_bytes();
};
