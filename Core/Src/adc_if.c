/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc_if.c
  * @author  MCD Application Team
  * @brief   Read status related to the chip (battery level, VREF, chip temperature)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "adc_if.h"
#include "sys_app.h"
/* USER CODE BEGIN Includes */
#include "adc_bat_meas.h" /* SYS_GetBatteryLevel/SYS_GetTemperatureLevel artik
                            * kendi ayri ADC_ReadChannels()/MX_ADC_Init() dongusunu
                            * calistirmiyor - bkz asagidaki fonksiyonlarin yorumu:
                            * ayni hadc/PB4 uzerinde adc_bat_meas.c ile CAKISAN,
                            * gorunmez bir ikinci tuketiciydi (LoRaMAC bunu
                            * DevStatusReq'e cevap hazirlarken kendiliginden
                            * cagiriyor - sunucudan DevStatusReq gelmesi tetikliyor,
                            * bizim STATUS olcumumuzle ayni ADC/PB4 donanimini
                            * bizden habersiz kullaniyordu). */
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/**
  * @brief ADC handle
  */
extern ADC_HandleTypeDef hadc;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/
/* USER CODE BEGIN EF */

/* USER CODE END EF */

void SYS_InitMeasurement(void)
{
  /* USER CODE BEGIN SYS_InitMeasurement_1 */

  /* USER CODE END SYS_InitMeasurement_1 */
  hadc.Instance = ADC;
  /* USER CODE BEGIN SYS_InitMeasurement_2 */

  /* USER CODE END SYS_InitMeasurement_2 */
}

void SYS_DeInitMeasurement(void)
{
  /* USER CODE BEGIN SYS_DeInitMeasurement_1 */

  /* USER CODE END SYS_DeInitMeasurement_1 */
}

/* SYS_GetTemperatureLevel/SYS_GetBatteryLevel ARTIK kendi ADC_ReadChannels()/
 * MX_ADC_Init() dongusunu calistirmiyor. Eskiden bu ikisi, adc_bat_meas.c'nin
 * kullandigi AYNI hadc/PB4 donanimini, TAMAMEN AYRI (ve farkli ayarlara sahip:
 * LowPowerAutoWait, Overrun modu) bir init/olcum dongusuyle okuyordu.
 * LoRaMAC bu iki fonksiyonu, sunucudan DevStatusReq geldiginde DevStatusAns
 * hazirlarken KENDILIGINDEN cagiriyor (bkz lora_app.c LmHandlerCallbacks_t.
 * GetBatteryLevel/.GetTemperature) - yani bizim STATUS mesajimiz icin
 * adc_bat_meas.c ile olcum yaparken, LoRaMAC'in bundan tamamen habersiz,
 * ayni ADC/PB4 donanimini kullanan gorunmez bir "ikinci tuketicisi" vardi.
 * Sahada gozlemlenen, hicbir donanim degisikligiyle (bolucu direnc, GPIO/GND
 * anahtari, filtre kapasitoru, gecikme) duzelmeyen tutarsiz batarya
 * okumalarinin en olasi nedeni buydu. Duzeltme: tek, ortak olcum yoluna
 * (adc_bat_meas.c) yonlendirmek - ADC'ye artik SADECE bir yerden dokunuluyor,
 * ayrica LoRaMAC'in DevStatusAns'i ile bizim STATUS mesajimiz artik ayni
 * degeri raporluyor. */
int16_t SYS_GetTemperatureLevel(void)
{
  /* USER CODE BEGIN SYS_GetTemperatureLevel_1 */

  /* USER CODE END SYS_GetTemperatureLevel_1 */
  int16_t temperatureQ8_8 = 0;
  (void)adc_conv_get_battery_volatge(&temperatureQ8_8);
  return temperatureQ8_8;
  /* USER CODE BEGIN SYS_GetTemperatureLevel_2 */

  /* USER CODE END SYS_GetTemperatureLevel_2 */
}

uint16_t SYS_GetBatteryLevel(void)
{
  /* USER CODE BEGIN SYS_GetBatteryLevel_1 */

  /* USER CODE END SYS_GetBatteryLevel_1 */
  int16_t temperatureQ8_8 = 0;
  return adc_conv_get_battery_volatge(&temperatureQ8_8);
  /* USER CODE BEGIN SYS_GetBatteryLevel_2 */

  /* USER CODE END SYS_GetBatteryLevel_2 */
}

/* Private Functions Definition -----------------------------------------------*/
/* USER CODE BEGIN PrFD */

/* USER CODE END PrFD */
