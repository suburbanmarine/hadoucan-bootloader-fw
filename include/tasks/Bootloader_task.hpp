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
		if(! m_fd )
		{
			return -1;
		}
		*m_fd = {};

		int ret = lfs_file_open(m_fs->get_fs(), get_fd(), path, flags);

		return ret;
	}

	int close()
	{
		int ret = lfs_file_close(m_fs->get_fs(), get_fd());
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

	// called from usb thread
	uint32_t handle_tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state);
	void handle_tud_dfu_download_cb (uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length);
	void handle_tud_dfu_manifest_cb(uint8_t alt);
	uint16_t handle_tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length);
	void handle_tud_dfu_detach_cb(void);
	void handle_tud_dfu_abort_cb(uint8_t alt);

protected:

	bool init_usb();

	void jump_to_addr(uint32_t estack, uint32_t jump_addr) __attribute__((noreturn));

	bool load_verify_hex_app_image();
	bool load_verify_bin_app_image();
	bool load_verify_bin_gcm_app_image();

	void sync_and_reset();
	W25Q16JV m_qspi;
	W25Q16JV_app_region m_fs;

	std::array<char, 25> usb_id_str;

	std::shared_ptr<LFS_file> m_fd;
	mbedtls_md5_context m_fd_md5_ctx;

	uint8_t* const m_mem_base          = reinterpret_cast<uint8_t*>(0x24000000);
	const size_t m_mem_size            = 512*1024*1024;
	const size_t m_download_block_size = CFG_TUD_DFU_XFER_BUFSIZE;

	bool check_option_bytes();
	bool config_option_bytes();
};
