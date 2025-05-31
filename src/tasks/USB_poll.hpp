#pragma once

#include "freertos_cpp_util/Task_static.hpp"

class USB_core_task : public Task_static<2048>
{
public:

	USB_core_task()
	{

	}

	void work() override;

private:
	
};
