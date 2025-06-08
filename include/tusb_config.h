#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#define CFG_TUD_ENABLED       OPT_MODE_DEVICE

#define CFG_TUD_MAX_SPEED     OPT_MODE_HIGH_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION __attribute__ (( section(".ram_dtcm_noload") ))
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE   64
#endif

#define CFG_TUD_DFU              1
#define CFG_TUD_DFU_XFER_BUFSIZE 512

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */