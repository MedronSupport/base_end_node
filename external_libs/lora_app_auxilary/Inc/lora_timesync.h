/**
  ******************************************************************************
  * @file    lora_timesync.h
  * @brief   Sunucudan (MQTT/downlink uzerinden) gelen zaman senkronizasyon
  *          mesajini isleyen, cihazin "su an ne zaman?" sorusuna cevap veren
  *          bagimsiz kucuk kutuphane.
  *
  * Bu modul lora_app.c'den bilerek AYRI tutulur: RFID okuma gibi uygulamaya
  * ozgu hicbir seye bagimli degildir, sadece LoRaWAN uzerinden zaman
  * senkronizasyonu isteyen herhangi bir projede (RFID'siz de) oldugu gibi
  * kullanilabilir.
  *
  * Zaman kaynagi olarak STM32CubeWL'in kendi SysTimeSet()/SysTimeGet()
  * (bkz Utilities/misc/stm32_systime.h) altyapisini kullanir. Bu altyapi,
  * "gercek zaman - cihazin kendi RTC sayaci" farkini RTC YEDEK
  * register'larina yazar (RTC_BKP_DR0/DR1 - bkz timer_if.c). Bu register'lar
  * VDD dusmedigi surece (ornegin bir watchdog reset'inde ya da yazilimsal
  * NVIC_SystemReset()'te) SILINMEZ - yani cihaz beklenmedik sekilde
  * yeniden basladiginda bile, senkron edilmis saat bilgisi KAYBOLMAZ.
  *
  * Bu modul ayrica kendi ayri bir yedek register'inda (RTC_BKP_DR3) bir
  * "gercekten senkron oldu mu" isareti tutar - cunku SysTime'in kendi
  * farki, hic senkron olunmamis olsa bile varsayilan olarak sifirdir; bu
  * isaret olmadan "hic senkron olunmadi" ile "fark sifir olacak sekilde
  * senkron olundu" durumlarini ayirt edemezdik.
  *
  * Kullanim sirasi:
  *   1) Uygulama baslangicinda bir kez  LoraTimeSync_Init()
  *   2) Her basarili (re)join sonrasi   LoraTimeSync_OnJoined()
  *   3) STATUS mesaji hazirlarken       LoraTimeSync_GetCounterForStatusSend()
  *      degerini payload'a goem; LmHandlerSend basariyla kuyruga alinirsa
  *                                      LoraTimeSync_OnStatusQueued()
  *   4) O status'un ACK sonucu belli olunca
  *                                      LoraTimeSync_OnStatusAckResult(ackReceived)
  *   5) Herhangi bir downlink geldiginde
  *                                      LoraTimeSync_HandleDownlink(...)
  *      cagir - kendi mesaji degilse dokunmadan false doner.
  *   6) Zaman lazim oldugunda           LoraTimeSync_GetCurrentUnixTime()
  ******************************************************************************
  * @attention
  *
  * Bu dosya proje icin yazilmistir, harici bir lisansi yoktur.
  ******************************************************************************
  */
#ifndef LORA_TIMESYNC_H
#define LORA_TIMESYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
  * @brief Sunucudan gelen zaman senkronizasyon downlink'inin sabit boyutu (byte).
  *
  * Format (big-endian):
  *   [0]    tip biti, bkz LORA_TIMESYNC_DOWNLINK_TYPE
  *   [1:4]  Unix epoch (saniye)
  *   [5:8]  sunucunun bu yaniti hesapladigi andaki status sayaci - tazelik
  *          kontrolu icin, bkz LoraTimeSync_HandleDownlink()
  */
#define LORA_TIMESYNC_DOWNLINK_SIZE   9U

/**
  * @brief Zaman senkronizasyon downlink'ini diger uygulama mesajlarindan
  *        ayirt etmek icin kullanilan tip biti (payload'in ilk byte'i).
  */
#define LORA_TIMESYNC_DOWNLINK_TYPE   0x01U

/**
  * @brief  Modulu kullanima hazirlar. Baska her fonksiyondan once, uygulama
  *         baslangicinda (LoRaWAN_Init benzeri bir yerde) bir kez cagrilmalidir.
  * @note   Onceden RTC yedek register'larina yazilmis senkron bilgisine
  *         (varsa) DOKUNMAZ - sadece bu modulun kendi RAM ici sayaclarini
  *         sifirlar. Yani cihaz resetlendiginde saat bilgisi bu cagridan
  *         etkilenmeden korunmaya devam eder.
  */
void LoraTimeSync_Init(void);

/**
  * @brief  Basarili her (re)join sonrasi cagrilmalidir. Status sayacini ve
  *         bekleyen retry durumunu sifirlar - sunucu tarafinin "join
  *         aninda status sayaci 0'dir" varsayimini her seferinde gecerli
  *         kilar. Bu cagrilmazsa, uzun sure calisip yuksek bir sayaca
  *         ulasmis bir cihaz rejoin olduktan sonra join-tetikli zaman
  *         senkronizasyon yanitini hatali sekilde "stale" sanip reddedebilir.
  */
void LoraTimeSync_OnJoined(void);

/**
  * @brief  Bir STATUS mesaji hazirlarken, payload'a gomulecek sayaç
  *         degerini dondurur.
  * @note   Onceki STATUS denemesinin ACK'i alinamayip retry bekleniyorsa,
  *         bu fonksiyon o denemeyle AYNI sayaci geri dondurur (sayaç
  *         ilerlemez) - boylece sunucudan GECIKMELI gelen bir yanit bile
  *         "taze" olarak taninip kabul edilebilir; cihazin saati en fazla
  *         bir retry periyodu kadar sapmis olur.
  * @retval Payload'a yazilacak uint32 sayaç degeri.
  */
uint32_t LoraTimeSync_GetCounterForStatusSend(void);

/**
  * @brief  Bir STATUS mesaji basariyla kuyruga alindiginda (ACK sonucundan
  *         BAGIMSIZ, sadece LmHandlerSend cagrisi basarili donduginde)
  *         cagrilmalidir. Sayaci "bu deneme artik gonderilmis" olarak
  *         isaretler.
  */
void LoraTimeSync_OnStatusQueued(void);

/**
  * @brief  En son kuyruga alinan STATUS mesajinin ACK sonucunu bildirir.
  * @param  ackReceived ACK alindiysa true, alinamadiysa false.
  * @note   false verilirse, bir sonraki LoraTimeSync_GetCounterForStatusSend()
  *         cagrisi ayni sayaci tekrar dondurecek sekilde bir retry durumu
  *         kurulur (bkz yukaridaki not).
  */
void LoraTimeSync_OnStatusAckResult(bool ackReceived);

/**
  * @brief  Gelen bir downlink'in zaman senkronizasyon mesaji olup olmadigini
  *         kontrol eder; oyle ise ve tazeyse (gomulu sayaç bizim en son
  *         gonderdigimiz STATUS'un sayaciyla birebir eslesirse) cihaz
  *         saatini gunceller.
  * @note   fPort kontrolu bu fonksiyonun disinda, cagiran taraf (OnRxData)
  *         tarafindan yapilmalidir - bu modul hangi port'un "uygulama
  *         portu" oldugunu bilmez, bilerek boyle tasarlanmistir.
  * @param  buffer Downlink payload'i (NULL olabilir).
  * @param  size   Downlink payload uzunlugu (byte).
  * @retval true   Mesaj kabul edilip saat guncellendi.
  * @retval false  Bu bir zaman senkronizasyon mesaji degildi, ya da
  *                stale/gecikmis oldugu icin reddedildi.
  */
bool LoraTimeSync_HandleDownlink(const uint8_t *buffer, uint8_t size);

/**
  * @brief  Cihazin en az bir kez basariyla senkronize olup olmadigini,
  *         reset sonrasi da dahil olmak uzere dogru sekilde bildirir.
  */
bool LoraTimeSync_IsSynced(void);

/**
  * @brief  Cihazin su anki tahmini Unix epoch degerini (saniye) dondurur.
  * @retval Senkronize degilse 0, senkronizeyse guncel epoch tahmini.
  */
uint32_t LoraTimeSync_GetCurrentUnixTime(void);

#ifdef __cplusplus
}
#endif

#endif /* LORA_TIMESYNC_H */
