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
#include "stm32_seq.h"
#include "stm32_timer.h"
#include "utilities_def.h"

/* (3749+1) * 256 / 32000 Hz(LSI) = 30.0 s. IWDG donanim tavani (prescaler=256,
 * reload=4095 ile) ~32,768 s - LSI kalibrasyonsuz bir RC osilator oldugu icin
 * bu tavana yaslanmiyoruz, ~2,77 s pay birakiyoruz. En uzun bloklu pencereden
 * (~5.6 s, RFID okuma) hala genis marjli - bkz app_watchdog.h basligi. */
#define APP_WATCHDOG_PRESCALER      IWDG_PRESCALER_256
#define APP_WATCHDOG_RELOAD         3749U

/* SADECE Stop2 GIRISINDE refresh etmek YETERLI DEGIL: IWDG, Stop2 uykusu
 * sirasinda da (LSI'den beslendigi icin) saymaya devam eder. Cihaz bir
 * sonraki olaya kadar (orn. saatlik/60 sn'lik status turu, ya da bir sonraki
 * RFID okutmasi) KESINTISIZ zaman asimindan uzun uyursa, PWR_EnterStopMode()
 * bir daha hic cagrilmadigi icin refresh de gelmez ve IWDG cihazi GERCEK BIR
 * DONMA YOKKEN resetler (bkz. saha loglarinda "BUFFER IS EMPTY" sonrasi
 * tekrarlayan reboot).
 *
 * Cozum: bagimsiz, periyodik bir "kick" gorevi. Enerji tasarrufu icin
 * mumkun oldugunca seyrek, ama zaman asiminin yaklasik 2/3'unden KISA
 * tutuluyor (20/30 = %67) ki normal sequencer gecikmeleri icin pay kalsin.
 * Gercek bir donmayi (orn. MFRC522 SPI'da HAL_MAX_DELAY ile kilitlenme)
 * yakalama ozelligini BOZMAZ: RTC alarm ISR'i bu timer'in suresi dolunca
 * sadece UTIL_SEQ_SetTask ile bir bayrak koyar (bkz.
 * AppWatchdog_OnKickTimerEvent) - asil HAL_IWDG_Refresh() cagrisi ancak
 * sequencer bu gorevi GERCEKTEN calistirabilirse olur. Sequencer donmussa
 * (orn. ana dongu bir blocking cagrida takiliysa) bu gorev hicbir zaman
 * calismaz ve IWDG yine de resetler. */
#define APP_WATCHDOG_KICK_PERIOD_MS 20000U

static IWDG_HandleTypeDef hiwdg;
static UTIL_TIMER_Object_t s_watchdogKickTimer;

static void AppWatchdog_KickTask(void)
{
    AppWatchdog_Refresh();
}

static void AppWatchdog_OnKickTimerEvent(void *context)
{
    (void)context;
    UTIL_SEQ_SetTask((1UL << CFG_SEQ_Task_WatchdogKickEvent), CFG_SEQ_Prio_0);
}

void AppWatchdog_Init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = APP_WATCHDOG_PRESCALER;
    hiwdg.Init.Reload = APP_WATCHDOG_RELOAD;
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;

    /* HAL_IWDG_Init hem yapilandirir hem de KR=0xCCCC yazarak IWDG'yi
     * baslatir - LSI'yi otomatik olarak (RCC uzerinden dokunmadan) devreye
     * sokar; bu satirdan sonra ilk AppWatchdog_Refresh() ~30 sn icinde
     * gelmezse cihaz resetlenir. */
    HAL_IWDG_Init(&hiwdg);

    UTIL_SEQ_RegTask((1UL << CFG_SEQ_Task_WatchdogKickEvent), UTIL_SEQ_RFU, AppWatchdog_KickTask);
    UTIL_TIMER_Create(&s_watchdogKickTimer, APP_WATCHDOG_KICK_PERIOD_MS, UTIL_TIMER_PERIODIC,
                       AppWatchdog_OnKickTimerEvent, NULL);
    UTIL_TIMER_Start(&s_watchdogKickTimer);
}

void AppWatchdog_Refresh(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
