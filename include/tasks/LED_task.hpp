#pragma once

#include "freertos_cpp_util/Task_static.hpp"
#include "freertos_cpp_util/BSema_static.hpp"

#include <atomic>

class LED_task : public Task_static<1024>
{
public:

	LED_task()
	{
		m_state = LED_STATE::BOOT;
	}

	enum LED_STATE
	{
		BOOT,
		WAIT_FOR_HOST
	};

	void set_state(const LED_STATE state)
	{
		m_state = state;
		m_state_semaphore.give();
	}

	void work() override;
private:

	void handle_state_boot();
	void handle_state_wait_for_reset();

	std::atomic<LED_STATE> m_state;
	BSema_static m_state_semaphore;
};