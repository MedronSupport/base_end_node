/**
  ******************************************************************************
  * @file    app_watchdog.c
  * @brief   app_watchdog.h implementasyonu.
  ******************************************************************************
  * @attention
  *
  * Bu dosya proje icin yazilmistir, harici bir lisansi yoktur.
  ******************************************************************************
  */
#include "app_watchdog.h"
#include "stm32wlxx_hal.h"

/* (3249+1) * 256 / 32000 Hz(LSI) = 26.0 s. Prescaler/reload tavani (IWDG
 * donanim siniri, ~32.7 s) ile en uzun blokli pencere (~5.6 s) arasinda
 * rahat bir marj birakir - bkz app_watchdog.h basligi. */
#define APP_WATCHDOG_PRESCALER   IWDG_PRESCALER_256
#define APP_WATCHDOG_RELOAD      3249U

static IWDG_HandleTypeDef hiwdg;

void AppWatchdog_Init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = APP_WATCHDOG_PRESCALER;
    hiwdg.Init.Reload = APP_WATCHDOG_RELOAD;
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;

    /* HAL_IWDG_Init hem yapilandirir hem de KR=0xCCCC yazarak IWDG'yi
     * baslatir - LSI'yi otomatik olarak (RCC uzerinden dokunmadan) devreye
     * sokar; bu satirdan sonra ilk AppWatchdog_Refresh() ~26 sn icinde
     * gelmezse cihaz resetlenir. */
    HAL_IWDG_Init(&hiwdg);
}

void AppWatchdog_Refresh(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
