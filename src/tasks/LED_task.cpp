#include "LED_task.hpp"

#include "main.h"
#include "stm32h7xx_hal_gpio.h"

void LED_task::work()
{
	HAL_GPIO_WritePin(GPIOD, GREEN1_Pin | RED1_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOD, GREEN2_Pin | RED2_Pin, GPIO_PIN_RESET);

	vTaskDelay(500);

	HAL_GPIO_WritePin(GPIOD, GREEN1_Pin | GREEN2_Pin, GPIO_PIN_SET);

	for(;;)
	{
		HAL_GPIO_WritePin(GPIOD, RED1_Pin | RED2_Pin, GPIO_PIN_SET);
		vTaskDelay(250);
		HAL_GPIO_WritePin(GPIOD, RED1_Pin | RED2_Pin, GPIO_PIN_RESET);
		vTaskDelay(250);
		HAL_GPIO_WritePin(GPIOD, RED1_Pin | RED2_Pin, GPIO_PIN_SET);
	}
}