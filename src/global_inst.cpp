#include "global_inst.hpp"

USB_core_task      usb_core_task      __attribute__ (( section(".ram_dtcm_noload") ));
USB_rx_buffer_task usb_rx_buffer_task __attribute__ (( section(".ram_dtcm_noload") ));
Logging_task       logging_task       __attribute__(( section(".ram_d2_s1_noload") ));
