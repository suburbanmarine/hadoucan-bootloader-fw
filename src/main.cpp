#include "main.h"
#include "cmsis_os.h"

// #include "uart1_printf.hpp"

#include "global_inst.hpp"

#include "freertos_cpp_util/Task_static.hpp"

#include "common_util/Byte_util.hpp"
#include "common_util/Stack_string.hpp"

#include <array>
#include <cinttypes>

void set_gpio_low_power(GPIO_TypeDef* const gpio)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_All;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	// GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(gpio, &GPIO_InitStruct);
}

void set_all_gpio_low_power()
{
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOF_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOI_CLK_ENABLE();
	__HAL_RCC_GPIOJ_CLK_ENABLE();
	__HAL_RCC_GPIOK_CLK_ENABLE();

	set_gpio_low_power(GPIOA);
	set_gpio_low_power(GPIOB);
	set_gpio_low_power(GPIOC);
	set_gpio_low_power(GPIOD);
	set_gpio_low_power(GPIOE);
	set_gpio_low_power(GPIOF);
	set_gpio_low_power(GPIOG);
	set_gpio_low_power(GPIOH);
	set_gpio_low_power(GPIOI);
	set_gpio_low_power(GPIOJ);
	set_gpio_low_power(GPIOK);

	__HAL_RCC_GPIOA_CLK_DISABLE();
	__HAL_RCC_GPIOB_CLK_DISABLE();
	__HAL_RCC_GPIOC_CLK_DISABLE();
	__HAL_RCC_GPIOD_CLK_DISABLE();
	__HAL_RCC_GPIOE_CLK_DISABLE();
	__HAL_RCC_GPIOF_CLK_DISABLE();
	__HAL_RCC_GPIOG_CLK_DISABLE();
	__HAL_RCC_GPIOH_CLK_DISABLE();
	__HAL_RCC_GPIOI_CLK_DISABLE();
	__HAL_RCC_GPIOJ_CLK_DISABLE();
	__HAL_RCC_GPIOK_CLK_DISABLE();
}

void halt_cpu()
{
	// Disable ISR and sync
	__asm__ volatile (
		"cpsid i\n"
		"isb\n"
		"dsb\n"
		: 
		: 
		: "memory"
	);

	// Only enabled ISR or events cause wake
	// Clear deep sleep register, sleep normal
	// Do not sleep on return to thread mode
	CLEAR_BIT (SCB->SCR, SCB_SCR_SEVONPEND_Msk | SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);

	for(;;)
	{
		// sync SCB write, WFI, sync/reload pipeline, enable ISR, sync/reload pipeline
		// certain platforms can crash on complex wfi return if no isb after wfi (arm core bug? - https://cliffle.com/blog/stm32-wfi-bug/)
		__asm__ volatile (
			"dsb\n"
			"wfi\n"
			"isb\n"
			"cpsie i\n"
			"isb\n"
			: 
			: 
			: "memory"
		);
	}
}

#ifdef SEMIHOSTING
extern "C"
{
	extern void initialise_monitor_handles();
}
#endif

int main(void)
{
	// If JTAG is attached, keep clocks on during sleep
	{
		if( (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0)
		{
			const uint32_t DBGMCU_CR = DBGMCU->CR;

			DBGMCU->CR = DBGMCU_CR
			  | DBGMCU_CR_DBG_SLEEPD1
			  | DBGMCU_CR_DBG_STOPD1
			  | DBGMCU_CR_DBG_STANDBYD1
			  // | DBGMCU_CR_DBG_TRACECKEN
			  | DBGMCU_CR_DBG_CKD1EN
			  | DBGMCU_CR_DBG_CKD3EN
			;
			__asm__ volatile (
				"dsb\n"
				: 
				: 
				: "memory"
			);
		}
	}

	// Handle errata
	{
		const uint32_t idcode = DBGMCU->IDCODE;
		const uint16_t rev_id = (idcode & 0xFFFF0000) >> 16;
		const uint16_t dev_id = (idcode & 0x000007FF);

		if(dev_id != 0x450)
		{
			// Only Dev ID STM32H7xx (42, 43/53, 50) is known
			halt_cpu();
		}

		switch(rev_id)
		{
			case 0x1003: // Rev Y
			{
				//errata 2.2.9
				uint32_t volatile * const AXI_TARG7_FN_MOD = 
				reinterpret_cast<uint32_t*>(
					0x51000000UL + // AXI Base
					0x1108UL +     // TARGx offset
					0x1000UL*7U    // Port 7, SRAM
				);

				const uint32_t AXI_TARGx_FN_MOD_READ_ISS_OVERRIDE  = 0x00000001;
				const uint32_t AXI_TARGx_FN_MOD_WRITE_ISS_OVERRIDE = 0x00000002;

				SET_BIT(*AXI_TARG7_FN_MOD, AXI_TARGx_FN_MOD_READ_ISS_OVERRIDE);
				__DSB();
			}
			case 0x2003: // Rev V
			{
				break;
			}
			case 0x1001: // Rev Z
			case 0x2001: // Rev X
			default:
			{
				halt_cpu();
				break;
			}
		}
	}

	// Enable semihosting if requested
	{
		#ifdef SEMIHOSTING
			initialise_monitor_handles();
		#endif
	}

	SCB_InvalidateDCache();
	SCB_InvalidateICache();

	// Check boot key early
	{
		Bootloader_key boot_key;
		boot_key.from_addr(reinterpret_cast<uint8_t const *>(0x38800000));

		std::array<uint8_t, 16> md5_axi = Bootloader_task::calculate_md5_axi_sram();

		if(boot_key.verify())
		{
			switch(boot_key.bootloader_op)
			{
				case uint8_t(Bootloader_key::Bootloader_ops::RUN_BOOTLDR):
				{
					break;
				}
				case uint8_t(Bootloader_key::Bootloader_ops::LOAD_APP):
				{
					break;
				}
				case uint8_t(Bootloader_key::Bootloader_ops::RUN_APP):
				{
					if( std::equal(boot_key.app_md5.begin(), boot_key.app_md5.end(), md5_axi.begin()) )
					{
						uint32_t app_estack = 0;
						uint32_t app_reset_handler = 0;

						volatile uint8_t* const axi_base = reinterpret_cast<volatile uint8_t*>(0x24000000);
						std::copy_n(axi_base, sizeof(app_estack), reinterpret_cast<uint8_t*>(&app_estack));
						std::copy_n(axi_base + sizeof(app_estack), sizeof(app_reset_handler), reinterpret_cast<uint8_t*>(&app_reset_handler));

						Bootloader_task::jump_to_addr(app_estack, app_reset_handler);
					}
					else
					{
						// Image is corrupt, reload
						Bootloader_task::set_bootloader_key(Bootloader_key::Bootloader_ops::LOAD_APP);
					}

					break;
				}
				default:
				{
					break;
				}
			}
		}
	}

	//confg mpu
	if(1)
	{
		/*
		ITCMRAM, 0x00000000, 64K

		FLASH, 0x08000000, 128K

		DTCMRAM, 0x20000000, 128K

		AXI_D1_SRAM, 0x24000000, 512K,  CPU Inst/Data

		AHB_D2_SRAM1, 0x30000000, 128K, CPU Inst
		AHB_D2_SRAM2, 0x30020000, 128K, CPU Data
		AHB_D2_SRAM3, 0x30040000, 32K,  Peripheral Buffers

		AHB_D3_SRAM4, 0x38000000, 64K

		BBRAM, 0x38800000, 4K

		QUADSPI, 0x90000000, 16M

		Peripherals, 0x40000000, 512M
		*/

		MPU_Region_InitTypeDef mpu_reg;
		
		HAL_MPU_Disable();

		/*
		// Global
		// Normal, no access
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER0;
		mpu_reg.BaseAddress = 0x00000000;
		mpu_reg.Size = MPU_REGION_SIZE_4GB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_NO_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL1;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);
		*/

		// ITCMRAM
		// Normal, Non-cacheable
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER1;
		mpu_reg.BaseAddress = 0x00000000;
		mpu_reg.Size = MPU_REGION_SIZE_64KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL1;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// FLASH
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER2;
		mpu_reg.BaseAddress = 0x08000000;
		mpu_reg.Size = MPU_REGION_SIZE_128KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_PRIV_RO_URO;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// DTCMRAM
		// Normal, Non-cacheable
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER3;
		mpu_reg.BaseAddress = 0x20000000;
		mpu_reg.Size = MPU_REGION_SIZE_128KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL1;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// AXI_D1_SRAM
		// Write-back, no write allocate
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER4;
		mpu_reg.BaseAddress = 0x24000000;
		mpu_reg.Size = MPU_REGION_SIZE_512KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// AHB_D2_SRAM1
		// Write-back, no write allocate
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER5;
		mpu_reg.BaseAddress = 0x30000000;
		mpu_reg.Size = MPU_REGION_SIZE_128KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// AHB_D2_SRAM2
		// Write-back, no write allocate
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER6;
		mpu_reg.BaseAddress = 0x30020000;
		mpu_reg.Size = MPU_REGION_SIZE_128KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// AHB_D2_SRAM3
		// Normal, Non-cacheable
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER7;
		mpu_reg.BaseAddress = 0x30040000;
		mpu_reg.Size = MPU_REGION_SIZE_32KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL1;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// AHB_D3_SRAM4
		// Write-back, no write allocate
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER8;
		mpu_reg.BaseAddress = 0x38000000;
		mpu_reg.Size = MPU_REGION_SIZE_64KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// BBSRAM
		// Write-back, no write allocate
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER9;
		mpu_reg.BaseAddress = 0x38800000;
		mpu_reg.Size = MPU_REGION_SIZE_4KB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// QUADSPI
		if(false)
		{
			// Write through, no write allocate
			mpu_reg.Enable = MPU_REGION_ENABLE;
			mpu_reg.Number = MPU_REGION_NUMBER10;
			mpu_reg.BaseAddress = 0x90000000;
			mpu_reg.Size = MPU_REGION_SIZE_16MB;
			mpu_reg.SubRegionDisable = 0x00;
			mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
			mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
			mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
			mpu_reg.IsCacheable = MPU_ACCESS_CACHEABLE;
			mpu_reg.IsBufferable = MPU_ACCESS_BUFFERABLE;
			mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		}
		else
		{
			// Due to errata 2.2.1, disable cache

			// Non-shareable device
			mpu_reg.Enable = MPU_REGION_ENABLE;
			mpu_reg.Number = MPU_REGION_NUMBER10;
			mpu_reg.BaseAddress = 0x90000000;
			mpu_reg.Size = MPU_REGION_SIZE_16MB;
			mpu_reg.SubRegionDisable = 0x00;
			mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
			mpu_reg.TypeExtField = MPU_TEX_LEVEL2;
			mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
			mpu_reg.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
			mpu_reg.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
			mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		}
		HAL_MPU_ConfigRegion(&mpu_reg);

		// Peripherals
		// Strongly Ordered
		/*
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER11;
		mpu_reg.BaseAddress = 0x40000000;
		mpu_reg.Size = MPU_REGION_SIZE_512MB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL0;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);
		*/
		// Non-shareable device 
		mpu_reg.Enable = MPU_REGION_ENABLE;
		mpu_reg.Number = MPU_REGION_NUMBER11;
		mpu_reg.BaseAddress = 0x40000000;
		mpu_reg.Size = MPU_REGION_SIZE_512MB;
		mpu_reg.SubRegionDisable = 0x00;
		mpu_reg.AccessPermission = MPU_REGION_FULL_ACCESS;
		mpu_reg.TypeExtField = MPU_TEX_LEVEL2;
		mpu_reg.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
		mpu_reg.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
		mpu_reg.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
		mpu_reg.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
		HAL_MPU_ConfigRegion(&mpu_reg);

		// Privledged code may use background mem map
		HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

		//No background mem map
		//MPU enabled during MMI
		// HAL_MPU_Enable(MPU_HARDFAULT_NMI);
		
	}

	SCB_EnableICache();

	SCB_EnableDCache();

	HAL_Init();

	//TODO: fix this to keep JTAG/SWD on, maybe
	// set_all_gpio_low_power();

	SystemClock_Config();

	//Enable backup domain in standby and Vbat mode
	HAL_PWREx_EnableBkUpReg();

	MX_GPIO_Init();
	MX_USART1_UART_Init();
	MX_RNG_Init();

	if(0)
	{
		/*Configure GPIO pin : PA8 */
		GPIO_InitTypeDef GPIO_InitStruct = {0};
		GPIO_InitStruct.Pin = GPIO_PIN_8;
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
		GPIO_InitStruct.Alternate = GPIO_AF0_MCO;
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

		HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_1);
	}

	bootloader_task.launch("bootloader_task", 2);
	led_task.launch("led", 2);

	vTaskStartScheduler();

	for(;;)
	{

	}

	return 0;
}
