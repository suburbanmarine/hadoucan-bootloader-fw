#include "tasks/LED_task.hpp"

#include "main.h"
#include "stm32h7xx_hal_gpio.h"

void LED_task::work()
{
	for(;;)
	{
		switch(m_state)
		{
			case LED_STATE::BOOT:
			{
				handle_state_boot();
				break;
			}
			case LED_STATE::WAIT_FOR_HOST:
			{
				handle_state_wait_for_reset();
				break;
			}
			default:
			{
				break;
			}
		}
		m_state_semaphore.try_take_for_ticks(pdMS_TO_TICKS(10));
	}
}

void LED_task::handle_state_boot()
{
	HAL_GPIO_WritePin(GPIOD, GREEN1_Pin | RED1_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOD, GREEN2_Pin | RED2_Pin, GPIO_PIN_RESET);
}
void LED_task::handle_state_wait_for_reset()
{
	HAL_GPIO_WritePin(GPIOD, GREEN1_Pin | GREEN2_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOD, RED1_Pin   | RED2_Pin,   GPIO_PIN_SET);
	if(m_state_semaphore.try_take_for_ticks(pdMS_TO_TICKS(250)))
	{
		return;
	}

	HAL_GPIO_WritePin(GPIOD, GREEN1_Pin | GREEN2_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOD, RED1_Pin   | RED2_Pin,   GPIO_PIN_RESET);
	if(m_state_semaphore.try_take_for_ticks(pdMS_TO_TICKS(250)))
	{
		return;
	}
}
