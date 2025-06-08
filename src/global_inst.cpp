#include "global_inst.hpp"

Bootloader_task    bootloader_task    __attribute__ (( section(".ram_dtcm_noload") ));
LED_task           led_task           __attribute__ (( section(".ram_dtcm_noload") ));
Logging_task       logging_task       __attribute__ (( section(".ram_dtcm_noload") ));
USB_core_task      usb_core_task      __attribute__ (( section(".ram_dtcm_noload") ));
