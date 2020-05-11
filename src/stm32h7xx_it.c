
#include "main.h"
#include "hal_inst.h"
#include "stm32h7xx_it.h"
#include "cmsis_os.h"

void NMI_Handler(void)
{

}

void HardFault_Handler(void)
{
  for(;;)
  {

  }
}

void MemManage_Handler(void)
{
  for(;;)
  {

  }
}

void BusFault_Handler(void)
{
  for(;;)
  {

  }
}

void UsageFault_Handler(void)
{
  for(;;)
  {

  }
}

void DebugMon_Handler(void)
{

}

void QUADSPI_IRQHandler(void)
{
  HAL_QSPI_IRQHandler(&hqspi);
}

void TIM17_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim17);
}
