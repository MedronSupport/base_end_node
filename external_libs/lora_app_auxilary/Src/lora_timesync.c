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
#include "stm32_timer.h"
#include "rtc.h"
#include "sys_app.h" /* APP_LOG - genel/uygulamadan bagimsiz bir izleme makrosu,
                      * bu modulun "lora_app.h'a bagimlilik yok" ilkesini bozmaz. */

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

/* Sayaç eslesmesi tek basina yeterli degil - gecikmis bir downlink, bir
 * sonraki STATUS turuna kadar (saatlerce) hala "ayni sayacli" gorunebilir.
 * Geriye sicramaya SABIT bir esik koymak yeterli degil: eger cihaz uzun
 * sure (gunlerce) hic senkron alamazsa, gercek RTC sapmasi (donanim
 * kaynakli, kristal toleransi/sicaklik) birikip mesru bir duzeltmenin de
 * sabit esigi asmasina yol acabilir - bu durumda dogru bir senkronu
 * yanlislikla reddederiz. Bunun yerine, gecen GERCEK sureyle ORANTILI bir
 * tolerans kullaniyoruz: taban (kisa sureli, network-gecikmesi kaynakli
 * sicramalari yakalayan) + gecen sureye gore RTC sapma payi. Sahada
 * gozlemlenen hata (32 dk'lik geri sicrama, ~30 dk gecmisken) bu modelde
 * de rahatlikla reddedilir (izin verilen tolerans hala saniyeler
 * mertebesinde kalir), ama gercekten haftalarca senkronsuz kalmis bir
 * cihazin ilk duzeltmesi haksiz yere reddedilmez. */
#define LORA_TIMESYNC_BASE_TOLERANCE_S       10UL
/* Kabul edilen azami RTC sapma orani (ppm) - LSE kristali icin bile
 * cok cok kotu bir durumu (sicaklik ucu, dusuk kaliteli parca) kapsayacak
 * kadar comert: 500 ppm = saatte ~1,8 sn, gunde ~43 sn, haftada ~5 dk. */
#define LORA_TIMESYNC_MAX_DRIFT_PPM          500UL

/* Son basarili senkronun cihaz uptime'i (UTIL_TIMER_GetCurrentTime, ms) -
 * geriye sicrama toleransini gecen GERCEK sureye gore olceklemek icin. RAM
 * ici (backup register'da degil) - reset sonrasi LoraTimeSync_Init() onu
 * "su an" ile sifirlar, yani reset hemen sonrasi tolerans BILEREK dar
 * tutulur (henuz hicbir senkron olmamis gibi davranilir). */
static uint32_t g_lastSyncUptimeMs = 0U;

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
    g_lastSyncUptimeMs = UTIL_TIMER_GetCurrentTime();
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
        APP_LOG(TS_OFF, VLEVEL_M,
                "###### TIME SYNC reddedildi: sayac uyusmuyor (gelen=%u, beklenen=%u, gelen epoch=%u)\r\n",
                (unsigned int)receivedCounter, (unsigned int)currentCounter, (unsigned int)receivedEpoch);
        return false;
    }

    /* Sayaç eşleşmesi TEK BAŞINA yeterli değil: ağ tarafı bir yanıtı
     * geciktirip, bir sonraki STATUS turuna kadar (saatlerce) hâlâ "aynı
     * sayaçlı" görünen ama gerçekte çok eski bir epoch ile teslim edebilir
     * (sahada gözlemlendi - bir yanıt kendi ACK penceresini kaçırıp ~32 dk
     * sonra tamamen ilgisiz bir RFID ACK'ine binerek geldi ve cihaz saatini
     * geriye sardı). Yeni epoch, cihazın zaten bildiği zamandan GERİYE
     * gidiyorsa, izin verilen payı son senkrondan bu yana geçen GERÇEK
     * süreye göre ölçekleyip karşılaştırıyoruz - bkz. yukarıdaki sabitler. */
    if (LoraTimeSync_IsSynced())
    {
        uint32_t currentEpoch = SysTimeGet().Seconds;
        if (currentEpoch > receivedEpoch)
        {
            uint32_t backwardJumpS = currentEpoch - receivedEpoch;
            uint32_t elapsedSinceSyncS = (UTIL_TIMER_GetCurrentTime() - g_lastSyncUptimeMs) / 1000U;
            uint32_t allowedToleranceS = LORA_TIMESYNC_BASE_TOLERANCE_S +
                (uint32_t)(((uint64_t)elapsedSinceSyncS * LORA_TIMESYNC_MAX_DRIFT_PPM) / 1000000UL);

            if (backwardJumpS > allowedToleranceS)
            {
                APP_LOG(TS_OFF, VLEVEL_M,
                        "###### TIME SYNC reddedildi: geriye sicrama cok buyuk (gelen epoch=%u, mevcut epoch=%u, sicrama=%u sn, izin verilen=%u sn)\r\n",
                        (unsigned int)receivedEpoch, (unsigned int)currentEpoch,
                        (unsigned int)backwardJumpS, (unsigned int)allowedToleranceS);
                return false;
            }
        }
    }

    SysTime_t syncedTime = { .Seconds = receivedEpoch, .SubSeconds = 0 };
    SysTimeSet(syncedTime);
    HAL_RTCEx_BKUPWrite(&hrtc, LORA_TIMESYNC_BKP_MAGIC_REG, LORA_TIMESYNC_BKP_MAGIC_VAL);
    g_lastSyncUptimeMs = UTIL_TIMER_GetCurrentTime();

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
