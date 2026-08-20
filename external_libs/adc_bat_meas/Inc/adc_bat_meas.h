/*
 * adv_bat_meas.h
 *
 *  Created on: Jul 31, 2026
 *      Author: fatih alparslan
 */

#ifndef ADV_BAT_MEAS_H_
#define ADV_BAT_MEAS_H_

#include "stm32wlxx_hal.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#define BAT_DIV_R_TOP_KOHM       110.0f
#define BAT_DIV_R_BOTTOM_KOHM    220.0f

#define VOLTAGE_DIV_COF \
    ((BAT_DIV_R_TOP_KOHM + BAT_DIV_R_BOTTOM_KOHM) / BAT_DIV_R_BOTTOM_KOHM)

void adc_bat_meas_init(void);
void BatteryDivider_Init(void);
void BatteryDivider_Enable(void);

void BatteryDivider_Disable(void);

void ADC_Select_CH3 (void);

void ADC_Select_VREF (void);

void ADC_Select_TEMP (void);


uint16_t readVREFINT_CAL(void);


uint16_t adc_conv_get_battery_volatge(int16_t *temperatureQ8_8);

#endif /* ADV_BAT_MEAS_H_ */
