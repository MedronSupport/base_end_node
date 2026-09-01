/**
  ******************************************************************************
  * @file    lora_timesync.c
  * @brief   lora_timesync.h implementasyonu.
  ******************************************************************************
  * @attention
  *
  * Bu dosya proje icin yazilmistir, harici bir lisansi yoktur.
  ******************************************************************************
  */
#include "lora_timesync.h"
#include "stm32_systime.h"
#include "rtc.h"

/**
  * @brief RTC handle - timer_if.c ile ayni global handle'i paylasir.
  */
extern RTC_HandleTypeDef hrtc;

/**
  * @brief SysTimeSet()/SysTimeGet() zaten RTC_BKP_DR0 (saniye farki) ve
  *        RTC_BKP_DR1'i (alt-saniye farki) kullaniyor, timer_if.c de ayrica
  *        RTC_BKP_DR2'yi (MSB tick uzatmasi) kullaniyor - bu modul cakismamak
  *        icin bir sonraki bos register olan DR3'u kullanir.
  *
  * Bu register, "SysTime farki gercekten sunucudan onaylanmis mi, yoksa
  * hala fabrika/varsayilan sifir mi" bilgisini tutar. SysTime'in kendisi
  * bu ayrimi yapamaz: hic senkron olunmamis olsa bile fark varsayilan
  * olarak sifirdir, yani SysTimeGet() her zaman "gecerli görünen" bir
  * deger doner - bu isaret olmadan "hic senkron olunmadi" durumunu tespit
  * edemeyiz.
  */
#define LORA_TIMESYNC_BKP_MAGIC_REG   RTC_BKP_DR3
#define LORA_TIMESYNC_BKP_MAGIC_VAL   0x53594E43UL /* ascii "SYNC" */

/* Kacinci STATUS gonderildigi (0'dan baslar, her basarili LmHandlerSend
 * kuyruklamasinda bir artar, her (re)join'de sifirlanir). Sadece TAZELIK
 * KONTROLU icin kullanilir - sunucu bu degeri time-sync yanitinda aynen
 * geri gonderir, biz de OnRxData'da bunu bizim en son gonderdigimiz
 * status'un sayaciyla karsilastirip SADECE ESITSE kabul ederiz. */
static uint32_t g_statusCounter = 0;

/* Bir STATUS gonderiminin ACK'i alinamazsa true olur; bir sonraki deneme
 * bu modulden AYNI sayaci ister (ilerlemez) - boylece sunucudan gecikmeli
 * gelen bir yanit bile "taze" olarak taninabilir. */
static bool g_resendPending = false;

void LoraTimeSync_Init(void)
{
    g_statusCounter = 0;
    g_resendPending = false;
}

void LoraTimeSync_OnJoined(void)
{
    g_statusCounter = 0;
    g_resendPending = false;
}

uint32_t LoraTimeSync_GetCounterForStatusSend(void)
{
    if (g_resendPending)
    {
        return (g_statusCounter > 0U) ? (g_statusCounter - 1U) : 0U;
    }
    return g_statusCounter;
}

void LoraTimeSync_OnStatusQueued(void)
{
    if (!g_resendPending)
    {
        g_statusCounter++;
    }
    g_resendPending = false;
}

void LoraTimeSync_OnStatusAckResult(bool ackReceived)
{
    g_resendPending = !ackReceived;
}

bool LoraTimeSync_HandleDownlink(const uint8_t *buffer, uint8_t size)
{
    if ((buffer == NULL) || (size != LORA_TIMESYNC_DOWNLINK_SIZE))
    {
        return false;
    }
    if (buffer[0] != (uint8_t)LORA_TIMESYNC_DOWNLINK_TYPE)
    {
        return false;
    }

    uint32_t receivedEpoch = ((uint32_t)buffer[1] << 24) |
                              ((uint32_t)buffer[2] << 16) |
                              ((uint32_t)buffer[3] << 8) |
                              ((uint32_t)buffer[4]);
    uint32_t receivedCounter = ((uint32_t)buffer[5] << 24) |
                                ((uint32_t)buffer[6] << 16) |
                                ((uint32_t)buffer[7] << 8) |
                                ((uint32_t)buffer[8]);

    /* g_statusCounter zaten bir sonraki gonderim icin bir arttirilmis
     * durumda - yanitin "su an" hangi status'a ait oldugunu bulmak icin
     * bir eksiltip karsilastiriyoruz. */
    uint32_t currentCounter = (g_statusCounter > 0U) ? (g_statusCounter - 1U) : 0U;

    if (receivedCounter != currentCounter)
    {
        /* stale/gecikmis bir yanit - bozuk bir epoch'u kabul edip cihaz
         * saatini yanlislikla kaydirmaktansa reddediyoruz. Bir sonraki
         * status turunde round-trip yetisirse taze bir yanit gelecektir. */
        return false;
    }

    SysTime_t syncedTime = { .Seconds = receivedEpoch, .SubSeconds = 0 };
    SysTimeSet(syncedTime);
    HAL_RTCEx_BKUPWrite(&hrtc, LORA_TIMESYNC_BKP_MAGIC_REG, LORA_TIMESYNC_BKP_MAGIC_VAL);

    return true;
}

bool LoraTimeSync_IsSynced(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, LORA_TIMESYNC_BKP_MAGIC_REG) == LORA_TIMESYNC_BKP_MAGIC_VAL;
}

uint32_t LoraTimeSync_GetCurrentUnixTime(void)
{
    if (!LoraTimeSync_IsSynced())
    {
        return 0U;
    }
    return SysTimeGet().Seconds;
}
