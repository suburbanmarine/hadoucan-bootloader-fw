#pragma once

#include "tasks/USB_rx_buffer_task.hpp"
#include "tasks/USB_poll.hpp"
#include "tasks/Logging_task.hpp"

extern USB_core_task      usb_core_task;
extern USB_rx_buffer_task usb_rx_buffer_task;
extern Logging_task       logging_task;
