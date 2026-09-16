/*
 * adc_bat_meas.c
 *
 *  Created on: Jul 31, 2026
 *      Author: fatih alparslan
 */

#include "adc_bat_meas.h"
#include "sys_app.h" /* APP_LOG - bkz asagidaki BAT ADC DEBUG satiri: bu
                      * modulun printf() cikislari __io_putchar() tanimsiz
                      * oldugu icin hicbir yere gitmiyordu (bkz Core/Src/
                      * syscalls.c - __io_putchar hicbir yerde define
                      * edilmemis), APP_LOG ise UART'a gittigi kanitlanmis
                      * tek yol - batarya olcum tanisi icin buna geçildi. */

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

     /*
      * ADC davranisini sade ve deterministik tutuyoruz.
      */
     hadc.Init.LowPowerAutoWait = DISABLE;
     hadc.Init.LowPowerAutoPowerOff = DISABLE;

     hadc.Init.ContinuousConvMode = DISABLE;
     hadc.Init.NbrOfConversion = 1;
     hadc.Init.DiscontinuousConvMode = DISABLE;

     hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
     hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;

     hadc.Init.DMAContinuousRequests = DISABLE;
     hadc.Init.Overrun = ADC_OVR_DATA_PRESERVED;

     /*
      * Hem harici divider hem de internal ADC kanallari icin
      * uzun sampling time.
      */
     hadc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;
     hadc.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_160CYCLES_5;

     hadc.Init.OversamplingMode = DISABLE;

     /*
      * Batarya olcumu seyrek yapildigi icin LOW frequency.
      */
     hadc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_LOW;

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


/* NOT (onemli): ADC_ChannelConfTypeDef.SamplingTime alani bu STM32 ailesinde
 * bir HAM cycle sabiti (ADC_SAMPLETIME_xxxCYCLES_5) DEGIL, HANGI ORTAK GRUBUN
 * (ADC_SAMPLINGTIME_COMMON_1/2 - bkz adc_bat_meas_init()'teki SamplingTimeCommon1/2)
 * kullanilacagini seçen bir SECICI bekliyor. Bu iki sabit ailesi FARKLI bit
 * araliklarinda (COMMON_1/2 bit>=8, cycle sabitleri bit 0-2) - asagida
 * ONCEDEN yanlislikla ADC_SAMPLETIME_160CYCLES_5 (bir cycle sabiti) verilmisti;
 * LL_ADC_SetChannelSamplingTime() bu degeri ADC_SAMPLING_TIME_CH_MASK ile
 * maskeleyince hicbir bit ORTUSMEDIGI icin sonuc her zaman sifir cikip
 * kanal SESSIZCE Grup 1'e (asagida artik 160.5 cycle olan) dusuyordu -
 * derleme zamaninda yakalanmadi cunku USE_FULL_ASSERT bu projede kapali.
 * Simdi dogru turde (COMMON_2) bir secici veriyoruz; ayrica adc_bat_meas_init()
 * icinde Grup 1'i de 160.5 cycle yaparak hata zararsiz hale getirildi -
 * hangi grup fiilen secilirse secilsin artik ayni (dogru, uzun) sure kullanilir. */
void ADC_Select_CH3 (void)
{
	ADC_ChannelConfTypeDef sConfig = {0};
	  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	  */
	  sConfig.Channel = ADC_CHANNEL_3;
	  sConfig.Rank = ADC_REGULAR_RANK_1;
	  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_2;
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
	  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_2;
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
	  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_2;
	  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
	  {
	    Error_Handler();
	  }
}


uint16_t readVREFINT_CAL(void) {
    // Read the 16-bit calibration value from the specified address
    return *VREFINT_CAL_ADDR;
}
/*
 * ADC'yi her conversion sonrasinda kapatmiyoruz.
 *
 * ADC ilk HAL_ADC_Start() ile enable olur,
 * tum discard + sample islemi boyunca enabled kalir.
 * En sonda bir kere HAL_ADC_Stop() yapilir.
 */
static uint16_t ADC_ReadAverage(uint8_t discardCount,
                                uint8_t sampleCount)
{
    uint32_t sum = 0;

    if (sampleCount == 0)
    {
        return 0;
    }

    uint32_t totalCount =
        (uint32_t)discardCount +
        (uint32_t)sampleCount;

    for (uint32_t i = 0; i < totalCount; i++)
    {
        if (HAL_ADC_Start(&hadc) != HAL_OK)
        {
            HAL_ADC_Stop(&hadc);
            return 0;
        }

        if (HAL_ADC_PollForConversion(&hadc, 1000) != HAL_OK)
        {
            HAL_ADC_Stop(&hadc);
            return 0;
        }

        uint16_t raw =
            (uint16_t)HAL_ADC_GetValue(&hadc);

        /*
         * Ilk conversion'lari kullanma.
         * Kanal/reference settle olmasina izin ver.
         */
        if (i >= discardCount)
        {
            sum += raw;
        }
    }

    /*
     * Tum ornekler tamamlandiktan sonra ADC'yi kapat.
     */
    HAL_ADC_Stop(&hadc);

    return (uint16_t)(sum / sampleCount);
}

uint16_t adc_conv_get_battery_volatge(int16_t *temperatureQ8_8)
{
    uint16_t VREFINT_CAL = 0;
    uint16_t VREF = 0;
    uint16_t AD_RES = 0;

    uint32_t vdda_mV = 0;
    uint32_t adcPin_mV = 0;
    uint32_t battMv = 0;


    /**************************************************************
     * ADC INIT
     **************************************************************/

    adc_bat_meas_init();


    /**************************************************************
     * BATTERY DIVIDER ENABLE
     **************************************************************/

    BatteryDivider_Init();
    BatteryDivider_Enable();

    /*
     * Divider ve ADC girisinin tamamen oturmasi icin.
     */
    HAL_Delay(100);


    /**************************************************************
     * ADC CALIBRATION
     **************************************************************/

    if (HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK)
    {
        APP_LOG(TS_OFF,
                VLEVEL_M,
                "###### BAT ADC ERROR: calibration failed\r\n");

        HAL_ADC_DeInit(&hadc);

        BatteryDivider_Disable();
        BatteryDivider_DeInit();

        return 0;
    }


    /**************************************************************
     * FACTORY VREFINT CALIBRATION VALUE
     **************************************************************/

    VREFINT_CAL = readVREFINT_CAL();


    /**************************************************************
     * VREFINT MEASUREMENT
     **************************************************************/

    ADC_Select_VREF();

    /*
     * Internal VREF path settle.
     */
    HAL_Delay(1);

    /*
     * Ilk 5 conversion discard.
     * Sonraki 16 conversion average.
     */
    VREF = ADC_ReadAverage(5, 16);

    if (VREF == 0)
    {
        APP_LOG(TS_OFF,
                VLEVEL_M,
                "###### BAT ADC ERROR: VREF read failed\r\n");

        HAL_ADC_DeInit(&hadc);

        BatteryDivider_Disable();
        BatteryDivider_DeInit();

        return 0;
    }


    /**************************************************************
     * VDDA CALCULATION
     *
     * VREFINT_CAL STM32WL'de 3.3V referansta kalibre edilmistir.
     *
     * VDDA = 3300 * VREFINT_CAL / VREF_RAW
     **************************************************************/

    vdda_mV =
        ((3300UL * (uint32_t)VREFINT_CAL)
         + ((uint32_t)VREF / 2UL))
        /
        (uint32_t)VREF;


    /**************************************************************
     * BATTERY ADC / PB4 / ADC_IN3
     **************************************************************/

    ADC_Select_CH3();

    /*
     * Kanal degisiminden sonraki ilk 5 conversion discard.
     * Sonraki 16 conversion average.
     */
    AD_RES = ADC_ReadAverage(5, 16);

    if (AD_RES == 0)
    {
        APP_LOG(TS_OFF,
                VLEVEL_M,
                "###### BAT ADC ERROR: CH3 read failed\r\n");

        HAL_ADC_DeInit(&hadc);

        BatteryDivider_Disable();
        BatteryDivider_DeInit();

        return 0;
    }


    /**************************************************************
     * ADC PIN VOLTAGE
     *
     * Vpin = RAW * VDDA / 4095
     **************************************************************/

    adcPin_mV =
        (((uint32_t)AD_RES * vdda_mV)
         + 2047UL)
        /
        4095UL;


    /**************************************************************
     * BATTERY VOLTAGE
     **************************************************************/

    battMv =
        (uint32_t)
        (
            ((float)adcPin_mV * VOLTAGE_DIV_COF)
            + 0.5f
        );


    /**************************************************************
     * INTERNAL TEMPERATURE
     **************************************************************/

    if (temperatureQ8_8 != NULL)
    {
        ADC_Select_TEMP();

        /*
         * Temperature kanalinda da ilk ornekleri at.
         */
        uint16_t tempRaw =
            ADC_ReadAverage(5, 8);

        if (tempRaw != 0)
        {
            int16_t temperatureDegreeC;

            if (((int32_t)*TEMPSENSOR_CAL2_ADDR -
                 (int32_t)*TEMPSENSOR_CAL1_ADDR) != 0)
            {
                temperatureDegreeC =
                    __LL_ADC_CALC_TEMPERATURE(
                        (uint16_t)vdda_mV,
                        tempRaw,
                        LL_ADC_RESOLUTION_12B);
            }
            else
            {
                temperatureDegreeC =
                    __LL_ADC_CALC_TEMPERATURE_TYP_PARAMS(
                        BAT_TEMPSENSOR_TYP_AVGSLOPE,
                        BAT_TEMPSENSOR_TYP_CAL1_V,
                        TEMPSENSOR_CAL1_TEMP,
                        (uint16_t)vdda_mV,
                        tempRaw,
                        LL_ADC_RESOLUTION_12B);
            }

            *temperatureQ8_8 =
                (int16_t)(temperatureDegreeC << 8);
        }
        else
        {
            *temperatureQ8_8 = 0;
        }
    }


    /**************************************************************
     * FINAL DEBUG
     **************************************************************/

    APP_LOG(
        TS_OFF,
        VLEVEL_M,
        "###### BAT ADC: "
        "raw=%u, "
        "vref=%u, "
        "vdda=%u mV, "
        "pin=%u mV, "
        "bat=%u mV\r\n",

        (unsigned int)AD_RES,
        (unsigned int)VREF,
        (unsigned int)vdda_mV,
        (unsigned int)adcPin_mV,
        (unsigned int)battMv
    );


    /**************************************************************
     * CLEANUP
     **************************************************************/

    HAL_ADC_DeInit(&hadc);

    BatteryDivider_Disable();
    BatteryDivider_DeInit();

    return (uint16_t)battMv;
}

