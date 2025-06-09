#pragma once

#include "freertos_cpp_util/Task_static.hpp"
#include "freertos_cpp_util/Event_group_static.hpp"

#include "tusb.h"

#include <atomic>

class USB_core_task : public Task_static<2048>
{
public:

	friend void tud_suspend_cb(bool remote_wakeup_en);
	friend void tud_resume_cb(void);
	friend void tud_mount_cb(void);
	friend void tud_umount_cb(void);

	USB_core_task()
	{

	}

	void work() override;

private:

	void handle_tud_suspend_cb(bool remote_wakeup_en);
	void handle_tud_resume_cb(void);
	void handle_tud_mount_cb(void);
	void handle_tud_umount_cb(void);

	const static EventBits_t USB_SUSPEND_BIT = 0x0002U;
	const static EventBits_t USB_MOUNTED_BIT = 0x0001U;
	Event_group_static m_events;
};
