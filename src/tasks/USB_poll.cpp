#include "tasks/USB_poll.hpp"

#include "global_inst.hpp"

#include "freertos_cpp_util/logging/Global_logger.hpp"

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

	#define USB_VID   0x6666
	#define USB_PID   0x6666
	#define USB_BCD   0x0200

	#define EPNUM_CDC_NOTIF   0x81
	#define EPNUM_CDC_OUT     0x02
	#define EPNUM_CDC_IN      0x82

	enum {
		STRID_LANGID = 0,
		STRID_MANUFACTURER,
		STRID_PRODUCT,
		STRID_SERIAL
	};

	#define DFU_FUNC_ATTRS (DFU_ATTR_CAN_UPLOAD | DFU_ATTR_CAN_DOWNLOAD | DFU_ATTR_MANIFESTATION_TOLERANT)
	#define DFU_ALT_COUNT 1

	#define ITF_NUM_DFU_MODE 0
	#define ITF_COUNT        1

	#define CONFIG_TOTAL_LEN TUD_CONFIG_DESC_LEN + TUD_DFU_DESC_LEN(DFU_ALT_COUNT)

	uint8_t const desc_configuration[] =
	{
		TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN, 0x00, 100),
		TUD_DFU_DESCRIPTOR(ITF_NUM_DFU_MODE, DFU_ALT_COUNT, 4, DFU_FUNC_ATTRS, 5000, CFG_TUD_DFU_XFER_BUFSIZE),
	};

	tusb_desc_device_t const desc_device = {
		.bLength            = sizeof(tusb_desc_device_t),
		.bDescriptorType    = TUSB_DESC_DEVICE,
		.bcdUSB             = USB_BCD,

		.bDeviceClass       = 0x00,
		.bDeviceSubClass    = 0x00,
		.bDeviceProtocol    = 0x00,

		.bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

		.idVendor           = USB_VID,
		.idProduct          = USB_PID,
		.bcdDevice          = 0x0100,

		.iManufacturer      = 0x01,
		.iProduct           = 0x02,
		.iSerialNumber      = 0x03,

		.bNumConfigurations = 0x01
	};

	uint8_t const * tud_descriptor_device_cb(void) {
		return (uint8_t const *) &desc_device;
	}

	uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
	{
		uint8_t const * ret = nullptr;
		switch(tud_speed_get())
		{
			case TUSB_SPEED_HIGH:
			case TUSB_SPEED_FULL:
			{
				ret = desc_configuration;
				break;
			}
			default:
			{
				ret = nullptr;
				break;
			}
		}

		return ret;
	}

	char const *string_desc_arr[] =
	{
		(const char[]) { 0x09, 0x04 }, // English
		"Suburban Marine, Inc.",       // 1: Manufacturer
		"HadouCAN Bootloader",         // 2: Product
		NULL,                          // 3: SN
		"FLASH"                        // 4: DFU ALT 0
	};

	void ascii_to_u16le(const size_t len, char const * const in, uint16_t* const out)
	{
		for(size_t i = 0; i < len; i++)
		{
			out[i+1] = in[i];
		}
	}

	static uint16_t desc_str_u16 [64 + 1];
	uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
	{
		size_t chr_count = 0;

		switch ( index )
		{
			case STRID_LANGID:
			{
				memcpy(&desc_str_u16[1], string_desc_arr[0], 2);
				chr_count = 1;
				break;
			}
			case STRID_SERIAL:
			{
				std::array<char, 25> id_str;
				Bootloader_task::get_unique_id_str(&id_str);

				chr_count = id_str.size() - 1;

				ascii_to_u16le(chr_count, id_str.data(), desc_str_u16);

				break;
			}
			default:
			{
				if ( !(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) ) return NULL;

				const char *str = string_desc_arr[index];

				chr_count = std::min(strlen(str), sizeof(desc_str_u16) / sizeof(desc_str_u16[0]) - 1);

				ascii_to_u16le(chr_count, str, desc_str_u16);

				break;
			}
		}

		desc_str_u16[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

		return desc_str_u16;
	}

	void tud_suspend_cb(bool remote_wakeup_en)
	{
		usb_core_task.handle_tud_suspend_cb(remote_wakeup_en);
	}

	void tud_resume_cb(void)
	{
		usb_core_task.handle_tud_resume_cb();
	}

	void tud_mount_cb(void)
	{
		usb_core_task.handle_tud_mount_cb();
	}
	void tud_umount_cb(void)
	{
		usb_core_task.handle_tud_umount_cb();
	}

	void USB_core_task::handle_tud_suspend_cb(bool remote_wakeup_en)
	{
		m_events.set_bits(USB_SUSPEND_BIT);
	}
	void USB_core_task::handle_tud_resume_cb(void)
	{
		m_events.clear_bits(USB_SUSPEND_BIT);
	}
	void USB_core_task::handle_tud_mount_cb(void)
	{
		m_events.set_bits(USB_MOUNTED_BIT);
	}
	void USB_core_task::handle_tud_umount_cb(void)
	{
		m_events.clear_bits(USB_MOUNTED_BIT);
	}

	uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state)
	{
		return bootloader_task.handle_tud_dfu_get_timeout_cb(alt, state);
	}
	void tud_dfu_download_cb(uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length)
	{
		bootloader_task.handle_tud_dfu_download_cb(alt, block_num, data, length);
	}
	void tud_dfu_manifest_cb(uint8_t alt)
	{
		bootloader_task.handle_tud_dfu_manifest_cb(alt);
	}
	uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length)
	{
		return bootloader_task.handle_tud_dfu_upload_cb(alt, block_num, data, length);
	}
	void tud_dfu_detach_cb(void)
	{
		bootloader_task.handle_tud_dfu_detach_cb();
	}
	void tud_dfu_abort_cb(uint8_t alt)
	{
		bootloader_task.handle_tud_dfu_abort_cb(alt);
	}
}
