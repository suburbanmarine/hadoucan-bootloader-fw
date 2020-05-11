#pragma once

#include "libusb_dev_cpp/core/usb_core.hpp"

#include "freertos_cpp_util/Task_static.hpp"

class USB_core_task : public Task_static<2048>
{
public:

	USB_core_task()
	{
		m_usb_core = nullptr;	
	}

	void set_usb_core(USB_core* const usb_core)
	{
		m_usb_core = usb_core;
	}

	void work() override;

private:
	USB_core* m_usb_core;
};

class USB_driver_task : public Task_static<2048>
{
public:

	USB_driver_task()
	{
		m_usb_core = nullptr;
	}

	void set_usb_core(USB_core* const usb_core)
	{
		m_usb_core = usb_core;
	}

	void work() override;

protected:
	USB_core* m_usb_core;
};

#include "freertos_cpp_util/BSema_static.hpp"
class USB_CDC_task : public Task_static<2048>
{
public:

	USB_CDC_task()
	{
		m_usb_core = nullptr;
	}

	void set_usb_core(USB_core* const usb_core)
	{
		m_usb_core = usb_core;
	}


	void work() override;

	void notify_new_connection();

protected:

	USB_core* m_usb_core;

	BSema_static m_new_connection;

};