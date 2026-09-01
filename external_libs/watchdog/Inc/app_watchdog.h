/**
  ******************************************************************************
  * @file    app_watchdog.h
  * @brief   IWDG (bagimsiz watchdog) sarmalayicisi.
  *
  * CubeMX .ioc'ta IWDG hicbir zaman etkinlestirilmedi (regenerate tetiklemek
  * istenmedi) - bu modul HAL_IWDG_Init/HAL_IWDG_Refresh'i CubeMX'in kendisi
  * cagirmis gibi elle baglar. Gereken tek CubeMX-disi adim, projeye elle
  * kopyalanan Drivers/STM32WLxx_HAL_Driver/{Inc,Src}/stm32wlxx_hal_iwdg.*
  * dosyalari ve Core/Inc/stm32wlxx_hal_conf.h'deki HAL_IWDG_MODULE_ENABLED
  * satiridir.
  *
  * Kullanim:
  *   1) Uygulama baslangicinda BIR KEZ  AppWatchdog_Init()  (main.c,
  *      /USER CODE BEGIN 2/ icinde, diger init'lerden sonra)
  *   2) Cihaz Stop2 moduna HER girisinde AppWatchdog_Refresh() (stm32_lpm_if.c,
  *      PWR_EnterStopMode() icinde, WFI'dan hemen once)
  *
  * NOT (bilincli tasarim karari): Error_Handler() (Core/Src/main.c) icine
  * KASITLI OLARAK refresh eklenmedi - oraya dusen bir hata artik IWDG
  * tarafindan yakalanip cihaz resetlenecek (sonsuza dek asili kalmak yerine).
  * Hata kalici ise (orn. pcb_init surekli basarisiz oluyorsa) bu, IWDG
  * periyodunda (~26 sn) tekrarlayan bir reset donguyune yol acar - bu
  * "sessizce asili kalmaktan" daha iyi kabul edilen, bilerek verilmis bir
  * karardir.
  ******************************************************************************
  * @attention
  *
  * Bu dosya proje icin yazilmistir, harici bir lisansi yoktur.
  ******************************************************************************
  */
#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  IWDG'yi ~26 saniyelik zaman asimiyla baslatir (LSI ~32 kHz,
  *         prescaler /256, reload 3249 -> (3249+1)*256/32000 = 26.0 s).
  * @note   Bu sure, koddaki en uzun "Stop moduna girilemez" penceresinden
  *         (RFID kart okuma dongusu, RfidReadTimeoutTimer=5000 ms + ~600 ms
  *         pay, bkz lora_app.c ReadRFIDCard) yaklasik 4-5 kat genistir.
  *         MFRC_RFID_READ_TIMEOUT degistirilirse bu marj yeniden
  *         degerlendirilmelidir.
  */
void AppWatchdog_Init(void);

/**
  * @brief  IWDG sayacini sifirlar (kick). PWR_EnterStopMode() disinda,
  *         baska hicbir yerden cagirma - watchdog'un asil gorevi
  *         sequencer'in gercekten Stop moduna donebildigini dogrulamaktir.
  */
void AppWatchdog_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCHDOG_H */
