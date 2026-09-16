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
/* BAT_DIV_R_TOP_KOHM, devre disi (multimetre ile) olculen 11.0k nominal
 * degerden BILEREK FARKLI: bu degerler artik "gercek direnc degeri" degil,
 * DOGRUDAN, ESZAMANLI iki gercek olcumle (batarya girisi=3540mV, ADC olcum
 * noktasi/bolucu ucu=2325mV, ikisi de multimetreyle ayni anda) ampirik
 * olarak KALIBRE EDILMIS bir oran temsil ediyor:
 *   gereken COF = 3540/2325 = 1,5226
 *   BAT_DIV_R_BOTTOM_KOHM=22.0 sabit tutulup TOP buna gore cozuldu.
 * Devre disi olculen "gercek" direnc degerleri (10.71/21.5 gibi) bu orani
 * TAM vermiyordu - aradaki kucuk fark muhtemelen ADC'nin kendi yukleme
 * etkisi/kalan tolerans gibi ayri ayri modellemesi zor kaynaklardan
 * geliyor. Bu yuzden direnc degerlerini "doğru" temsil etmeye calismak
 * yerine, DOGRUDAN COF'u gercek olcumlere gore kalibre ediyoruz. */
#define BAT_DIV_R_TOP_KOHM       11.50f
#define BAT_DIV_R_BOTTOM_KOHM    22.0f

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
