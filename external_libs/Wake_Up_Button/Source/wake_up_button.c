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
void awake_led_gpio_init(void)
{
	  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = AWAKE_LED_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(AWAKE_LED_PORT, &GPIO_InitStruct);


  HAL_GPIO_WritePin(AWAKE_LED_PORT, AWAKE_LED_PIN, GPIO_PIN_RESET);

}
void awake_led_gpio_deinit(void)
{
	HAL_GPIO_WritePin(AWAKE_LED_PORT, AWAKE_LED_PIN, GPIO_PIN_RESET);
	HAL_GPIO_DeInit(AWAKE_LED_PORT, AWAKE_LED_PIN);
}
void awake_led_gpio_toggle(void)
{
	HAL_GPIO_TogglePin(AWAKE_LED_PORT, AWAKE_LED_PIN);
}
void BuzzerNotify_init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = Buzzer_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(Buzzer_PORT, &GPIO_InitStruct);


  HAL_GPIO_WritePin(Buzzer_PORT, Buzzer_PIN, GPIO_PIN_RESET);

}
void BuzzerNotify_deinit(void)
{
	HAL_GPIO_WritePin(Buzzer_PORT, Buzzer_PIN, GPIO_PIN_RESET);
	HAL_Delay(5);
	HAL_GPIO_DeInit(Buzzer_PORT, Buzzer_PIN);
}
void Buzzer_Alert_Process(uint32_t ms)
{
	HAL_GPIO_WritePin(Buzzer_PORT, Buzzer_PIN, GPIO_PIN_SET);
	HAL_Delay(ms);
	HAL_GPIO_WritePin(Buzzer_PORT, Buzzer_PIN, GPIO_PIN_RESET);

}
