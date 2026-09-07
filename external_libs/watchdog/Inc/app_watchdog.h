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
  *      /USER CODE BEGIN 2/ icinde, diger init'lerden sonra) - bu ayrica
  *      kendi periyodik "kick" timer'ini da kurup baslatir (asagiya bkz).
  *   2) Cihaz Stop2 moduna HER girisinde AppWatchdog_Refresh() (stm32_lpm_if.c,
  *      PWR_EnterStopMode() icinde, WFI'dan hemen once)
  *
  * NOT (saha loglarindan bulunan gercek bug, duzeltildi): IWDG, Stop2
  * uykusu sirasinda da (LSI baglı oldugu icin) saymaya devam eder. Cihaz
  * bir sonraki olaya kadar (orn. periyodik status turu, ya da bir sonraki
  * RFID okutmasi) zaman asimindan UZUN, KESINTISIZ uyursa, madde 2'deki
  * refresh bir daha hic cagrilmaz ve IWDG cihazi GERCEK BIR DONMA OLMADAN
  * resetler. Bu yuzden AppWatchdog_Init() kendi basina, ayrica bagimsiz bir
  * periyodik (~20 sn) "kick" gorevi de kurar - bu, en uzun bekleme
  * suresinden BAGIMSIZ olarak IWDG'yi taze tutar (ve enerji tasarrufu icin
  * zaman asiminin ~2/3'u kadar seyrek tutulur), ama gercek bir donmayi
  * (orn. MFRC522 SPI'da kilitlenme) yakalama ozelligini bozmaz: RTC ISR
  * sadece bir sequencer gorevi bayragi koyar, asil refresh ancak sequencer
  * bu gorevi CALISTIRABILIRSE olur - donmus bir sequencer bu gorevi hic
  * calistiramaz.
  *
  * NOT (bilincli tasarim karari): Error_Handler() (Core/Src/main.c) icine
  * KASITLI OLARAK refresh eklenmedi - oraya dusen bir hata artik IWDG
  * tarafindan yakalanip cihaz resetlenecek (sonsuza dek asili kalmak yerine).
  * Hata kalici ise (orn. pcb_init surekli basarisiz oluyorsa) bu, IWDG
  * periyodunda (~30 sn) tekrarlayan bir reset donguyune yol acar - bu
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
  * @brief  IWDG'yi ~30 saniyelik zaman asimiyla baslatir (LSI ~32 kHz,
  *         prescaler /256, reload 3749 -> (3749+1)*256/32000 = 30.0 s) ve
  *         kendi bagimsiz periyodik (~20 sn) kick gorevini kurup baslatir.
  * @note   30 sn, koddaki en uzun "Stop moduna girilemez" penceresinden
  *         (RFID kart okuma dongusu, RfidReadTimeoutTimer=5000 ms + ~600 ms
  *         pay, bkz lora_app.c ReadRFIDCard) yaklasik 5-6 kat genistir, ve
  *         IWDG donanim tavanina (~32,768 sn) ~2,77 sn pay birakir.
  *         MFRC_RFID_READ_TIMEOUT degistirilirse bu marj yeniden
  *         degerlendirilmelidir.
  */
void AppWatchdog_Init(void);

/**
  * @brief  IWDG sayacini sifirlar (kick). Cagiranlar: PWR_EnterStopMode()
  *         (her Stop2 girisinde) ve AppWatchdog_Init()'in kurdugu periyodik
  *         kick gorevi (~20 sn) - baska bir yerden cagirmaya gerek yok.
  */
void AppWatchdog_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCHDOG_H */
