#include "tasks/Logging_task.hpp"

#include "uart1_printf.hpp"

#include "hal_inst.h"
#include "stm32h7xx_hal.h"

#include <cstdio>

bool UART1_sink::handle_log(freertos_util::logging::String_type* const log)
{
	HAL_StatusTypeDef uartret;
	{
		std::lock_guard<Mutex_static> lock(m_uart1_mutex);
		uartret = HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(const_cast<char*>(log->c_str())), log->size(), -1);
	}

	return uartret == HAL_OK;
}

bool Semihosting_sink::handle_log(freertos_util::logging::String_type* const log)
{
	size_t uartret;
	{
		std::lock_guard<Mutex_static> lock(m_uart1_mutex);
		uartret = ::fwrite(log->c_str(), 1, log->size(), stdout);
	}

	return uartret == log->size();
}

void Logging_task::work()
{
	// freertos_util::logging::Logger* const log = freertos_util::logging::Global_logger::get_instance().get_log();

	for(;;)
	{
		// log->process_one();
		global_logs.process_one();
	}
}
