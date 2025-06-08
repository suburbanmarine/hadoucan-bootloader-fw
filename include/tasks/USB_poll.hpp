#pragma once

#include "freertos_cpp_util/Task_static.hpp"
#include "freertos_cpp_util/Event_group_static.hpp"

#include "tusb.h"

#include <atomic>

class USB_core_task : public Task_static<2048>
{
	friend void tud_suspend_cb(bool remote_wakeup_en);
	friend void tud_resume_cb(void);
	friend void tud_mount_cb(void);
	friend void tud_umount_cb(void);

	friend uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state);
	friend void tud_dfu_download_cb (uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length);
	friend void tud_dfu_manifest_cb(uint8_t alt);
	friend uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length);
	friend void tud_dfu_detach_cb(void);
	friend void tud_dfu_abort_cb(uint8_t alt);

public:

	USB_core_task()
	{

	}

	void work() override;

	std::atomic<bool> m_attach;
	
private:
	const static EventBits_t DFU_ATTACH_BIT = 0x0001U;
	Event_group_static m_events;
};
