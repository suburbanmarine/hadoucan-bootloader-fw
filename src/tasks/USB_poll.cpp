#include "USB_poll.hpp"

#include "uart1_printf.hpp"

#include "libusb_dev_cpp/class/cdc/cdc_notification.hpp"

// using freertos_util::logging::Global_logger;
// using freertos_util::logging::LOG_LEVEL;

void USB_core_task::work()
{
	// TODO: switch to isr mode
	// HAL_NVIC_SetPriority(OTG_HS_IRQn, 5, 0);
	// HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

	for(;;)
	{
		// usb_core.poll_driver();
		// usb_core.poll_event_loop();
		m_usb_core->wait_event_loop();
		taskYIELD();
	}
}

void USB_driver_task::work()
{
	for(;;)
	{
		m_usb_core->poll_driver();
		taskYIELD();
		// suspend();
	}
}


void USB_CDC_task::work()
{
	// freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();

	NETWORK_CONNECTION_NOTIFICATION net_conn_notify;
	net_conn_notify.set_connection(true);

	usb_driver_base* const usb_driver = m_usb_core->get_driver();

	for(;;)
	{
		if(!m_new_connection.take())
		{
			continue;
		}

		Buffer_adapter_base* tx_buf = usb_driver->wait_tx_buffer(0x82);
		tx_buf->reset();
		if(!net_conn_notify.serialize(tx_buf))
		{
			// logger->log(freertos_util::logging::LOG_LEVEL::FATAL, "USB_CDC_task", "net_conn_notify serialize failed");
			suspend();
		}

		// logger->log(freertos_util::logging::LOG_LEVEL::INFO, "USB_CDC_task", "sending net_conn_notify");
		if(!usb_driver->enqueue_tx_buffer(0x82, tx_buf))
		{
			// logger->log(freertos_util::logging::LOG_LEVEL::ERROR, "USB_CDC_task", "failed to enqueue tx buffer");
		}
	}
}

void USB_CDC_task::notify_new_connection()
{
	if(!m_new_connection.give())
	{
		// freertos_util::logging::Logger* const logger = freertos_util::logging::Global_logger::get();
		// logger->log(freertos_util::logging::LOG_LEVEL::FATAL, "USB_CDC_task", "failed to give m_new_connection");
	}
}
