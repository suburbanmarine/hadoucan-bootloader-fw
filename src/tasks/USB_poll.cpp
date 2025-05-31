#include "USB_poll.hpp"

#include "freertos_cpp_util/logging/Global_logger.hpp"

#include "tusb.h"

// using freertos_util::logging::Global_logger;
using freertos_util::logging::LOG_LEVEL;

void USB_core_task::work()
{
	for(;;)
	{
		tud_task();

		taskYIELD();
	}
}

extern "C"
{
	void OTG_FS_IRQHandler(void)
	{
		tusb_int_handler(0, true);
	}

	void OTG_HS_IRQHandler(void)
	{
		tusb_int_handler(1, true);
	}
}
