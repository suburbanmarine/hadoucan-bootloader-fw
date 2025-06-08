#pragma once

#include "tasks/USB_poll.hpp"
#include "tasks/Logging_task.hpp"
#include "tasks/Bootloader_task.hpp"
#include "tasks/LED_task.hpp"

extern Bootloader_task    bootloader_task;
extern LED_task           led_task;
extern Logging_task       logging_task;
extern USB_core_task      usb_core_task;
