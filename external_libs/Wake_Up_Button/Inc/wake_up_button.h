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
#define  WakeUpButton_EXTI_IRQn EXTI15_10_IRQn

#define WAKE_UP_BUTTON_DEBOUNCE_MS  250U
void wake_up_gpio_init(void);



#endif /* EXTERNAL_LIBS_WAKE_UP_BUTTON_INC_WAKE_UP_BUTTON_H_ */
