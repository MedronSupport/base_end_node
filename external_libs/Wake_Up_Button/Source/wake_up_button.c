/*
 * wake_up_button.c
 *
 *  Created on: Aug 3, 2026
 *      Author: fatih
 */
#include "wake_up_button.h"
void wake_up_gpio_init(void){
		GPIO_InitTypeDef GPIO_InitStruct = {0};
	  __HAL_RCC_GPIOC_CLK_ENABLE();
	  /*Configure GPIO pin : BUT1_Pin */
	  GPIO_InitStruct.Pin = WakeUpButtonPin;
	  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	  GPIO_InitStruct.Pull = GPIO_PULLUP;
	  HAL_GPIO_Init(WakeUpButtonPort, &GPIO_InitStruct);
	  /* EXTI interrupt init*/
	  __HAL_GPIO_EXTI_CLEAR_IT(WakeUpButtonPin);
	  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
	  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}
