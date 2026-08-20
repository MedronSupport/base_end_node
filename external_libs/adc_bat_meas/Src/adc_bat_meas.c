/*
 * adc_bat_meas.c
 *
 *  Created on: Jul 31, 2026
 *      Author: fatih alparslan
 */

#include "adc_bat_meas.h"

 extern  ADC_HandleTypeDef hadc;


#define BAT_TEMPSENSOR_TYP_CAL1_V          (( int32_t)  760)
#define BAT_TEMPSENSOR_TYP_AVGSLOPE        (( int32_t) 2500)

void adc_bat_meas_init(void)
{
	__HAL_RCC_GPIOA_CLK_ENABLE();
	  hadc.Instance = ADC;
	  hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
	  hadc.Init.Resolution = ADC_RESOLUTION_12B;
	  hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	  hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
	  hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
	  hadc.Init.LowPowerAutoWait = ENABLE;
	  hadc.Init.LowPowerAutoPowerOff = DISABLE;
	  hadc.Init.ContinuousConvMode = DISABLE;
	  hadc.Init.NbrOfConversion = 1;
	  hadc.Init.DiscontinuousConvMode = DISABLE;
	  hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	  hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
	  hadc.Init.DMAContinuousRequests = DISABLE;
	  hadc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
	  hadc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_19CYCLES_5;
	  hadc.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_160CYCLES_5;
	  hadc.Init.OversamplingMode = DISABLE;
	  hadc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
	  if (HAL_ADC_Init(&hadc) != HAL_OK)
	  {
	    Error_Handler();
	  }
}
void BatteryDivider_Init(void)
{
	  __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Open-drain transistor kapalı: pin yüksek empedans
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
}
void BatteryDivider_DeInit(void)
{
	HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0);
}
void BatteryDivider_Enable(void)
{
    // Open-drain transistor açık: pin GND'ye çekilir
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
}

void BatteryDivider_Disable(void)
{
    // Pin serbest bırakılır, 3.3 V sürülmez
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
}


void ADC_Select_CH3 (void)
{
	ADC_ChannelConfTypeDef sConfig = {0};
	  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	  */
	  sConfig.Channel = ADC_CHANNEL_3;
	  sConfig.Rank = ADC_REGULAR_RANK_1;
	  sConfig.SamplingTime = ADC_SAMPLETIME_160CYCLES_5;
	  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
	  {
	    Error_Handler();
	  }
}

void ADC_Select_VREF (void)
{
	ADC_ChannelConfTypeDef sConfig = {0};
	  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	  */
	  sConfig.Channel = ADC_CHANNEL_VREFINT;
	  sConfig.Rank = ADC_REGULAR_RANK_1;
	  sConfig.SamplingTime = ADC_SAMPLETIME_160CYCLES_5;
	  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
	  {
	    Error_Handler();
	  }
}

void ADC_Select_TEMP (void)
{
	ADC_ChannelConfTypeDef sConfig = {0};
	  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	  */
	  sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
	  sConfig.Rank = ADC_REGULAR_RANK_1;
	  sConfig.SamplingTime = ADC_SAMPLETIME_160CYCLES_5;
	  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
	  {
	    Error_Handler();
	  }
}


uint16_t readVREFINT_CAL(void) {
    // Read the 16-bit calibration value from the specified address
    return *VREFINT_CAL_ADDR;
}

uint16_t adc_conv_get_battery_volatge(int16_t *temperatureQ8_8)
{
	adc_bat_meas_init();

	BatteryDivider_Init();

	BatteryDivider_Enable();
	HAL_Delay(100);
	uint16_t AD_RES_Arr[10] = {0};
	volatile uint16_t AD_RES = 0;
	uint32_t AD_RES_SUM=0;
	uint16_t VREF = 0;
	float VREF_CALC=0;

	uint16_t VREFINT_CAL=readVREFINT_CAL();
	HAL_ADCEx_Calibration_Start(&hadc);
	ADC_Select_CH3();
	for (int i = 0; i < 10; i++)
	{
		HAL_ADC_Start(&hadc);
		HAL_ADC_PollForConversion(&hadc, 1000);
		AD_RES_Arr[i] = HAL_ADC_GetValue(&hadc);
		if(i>=5)
		{
			AD_RES_SUM+=AD_RES_Arr[i];
			//printf("\nAD_RES_SUM %ld\n",AD_RES_SUM);
		}

		//printf("\nIN1 %d\n",AD_RES_Arr[i]);
	}

	//printf("\n F IN1 %ld\n",AD_RES_SUM);
	AD_RES=AD_RES_SUM/5;
	printf("\n RAW VALUE %d\n",AD_RES);
	HAL_ADC_Stop(&hadc);
	ADC_Select_VREF();
	HAL_ADC_Start(&hadc);
	HAL_ADC_PollForConversion(&hadc, 1000);
	VREF = HAL_ADC_GetValue(&hadc);
	HAL_ADC_Stop(&hadc);


	VREF_CALC = 3.3f * VREFINT_CAL / VREF;

	if (temperatureQ8_8 != NULL)
	{
		ADC_Select_TEMP();
		HAL_ADC_Start(&hadc);
		HAL_ADC_PollForConversion(&hadc, 1000);
		uint32_t tempRaw = HAL_ADC_GetValue(&hadc);
		HAL_ADC_Stop(&hadc);

		uint16_t vdda_mV = (uint16_t)(VREF_CALC * 1000.0f);
		int16_t temperatureDegreeC;
		if (((int32_t)*TEMPSENSOR_CAL2_ADDR - (int32_t)*TEMPSENSOR_CAL1_ADDR) != 0)
		{

			temperatureDegreeC = __LL_ADC_CALC_TEMPERATURE(vdda_mV, tempRaw, LL_ADC_RESOLUTION_12B);
		}
		else
		{

			temperatureDegreeC = __LL_ADC_CALC_TEMPERATURE_TYP_PARAMS(BAT_TEMPSENSOR_TYP_AVGSLOPE,
			                                                          BAT_TEMPSENSOR_TYP_CAL1_V,
			                                                          TEMPSENSOR_CAL1_TEMP,
			                                                          vdda_mV, tempRaw, LL_ADC_RESOLUTION_12B);
		}

		*temperatureQ8_8 = (int16_t)(temperatureDegreeC << 8);
	}


	HAL_ADC_DeInit(&hadc);

	BatteryDivider_Disable();
	BatteryDivider_DeInit();
	printf("VREF %d\n",VREF);
	printf("VREFINT_CAL %d\n",VREFINT_CAL);
	float Vin1=(VREF_CALC/4095.0)*AD_RES;
	return (uint16_t)(Vin1*VOLTAGE_DIV_COF*1000);

}

