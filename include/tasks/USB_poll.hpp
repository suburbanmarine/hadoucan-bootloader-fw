#pragma once

#include "freertos_cpp_util/Task_static.hpp"

#include <atomic>

class USB_core_task : public Task_static<2048>
{
public:

	USB_core_task() : m_dtr(false), m_rts(false), m_bit_rate(0), m_stop_bits(0), m_parity(0), m_data_bits(0), m_rx_avail(false), m_tx_complete(false)
	{

	}

	void work() override;

	std::atomic<bool> m_dtr;
	std::atomic<bool> m_rts;

	std::atomic<unsigned> m_bit_rate;
	std::atomic<unsigned> m_stop_bits;
	std::atomic<unsigned> m_parity;
	std::atomic<unsigned> m_data_bits;

	void wait_for_usb_rx_avail();
	void wait_for_usb_tx_complete();
	
private:
	std::atomic<bool> m_rx_avail;
	std::atomic<bool> m_tx_complete;
};
