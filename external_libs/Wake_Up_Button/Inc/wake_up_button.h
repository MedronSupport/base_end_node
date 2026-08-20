/*
 * wake_up_button.h
 *
 *  Created on: Aug 3, 2026
 *      Author: fatih alparslan
 */

#ifndef EXTERNAL_LIBS_WAKE_UP_BUTTON_INC_WAKE_UP_BUTTON_H_
#define EXTERNAL_LIBS_WAKE_UP_BUTTON_INC_WAKE_UP_BUTTON_H_

#include "stm32wlxx_hal.h"
#define WakeUpButtonPin GPIO_PIN_0
#define WakeUpButtonPort GPIOC

#define AWAKE_LED_PIN GPIO_PIN_3
#define AWAKE_LED_PORT GPIOA

#define Buzzer_PIN GPIO_PIN_1
#define Buzzer_PORT GPIOC

#define  WakeUpButton_EXTI_IRQn EXTI15_10_IRQn

#define WAKE_UP_BUTTON_DEBOUNCE_MS  250U
void wake_up_gpio_init(void);

void awake_led_gpio_init(void);
void awake_led_gpio_deinit(void);
void awake_led_gpio_toggle(void);


void BuzzerNotify_init(void);
void BuzzerNotify_deinit(void);
void Buzzer_Alert_Process(uint32_t ms);
#endif /* EXTERNAL_LIBS_WAKE_UP_BUTTON_INC_WAKE_UP_BUTTON_H_ */
