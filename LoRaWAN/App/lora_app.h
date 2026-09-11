/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    lora_app.h
  * @author  MCD Application Team
  * @brief   Header of application of the LRWAN Middleware
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __LORA_APP_H__
#define __LORA_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
typedef enum
{
	LSMS_TX_ACK_NONE=11,
	LSMS_TX_ACK_SUCCES=12,
	LSMS_TX_ACK_FAIL=13
}lora_sended_msg_status;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/

/* LoraWAN application configuration (Mw is configured by lorawan_conf.h) */
#define ACTIVE_REGION                               LORAMAC_REGION_EU868

/* USER CODE BEGIN EC_CAYENNE_LPP */
/*!
 * CAYENNE_LPP is myDevices Application server.
 */
/*#define CAYENNE_LPP*/
/* USER CODE END EC_CAYENNE_LPP */

/*!
 * Defines the application data transmission duty cycle. 10s, value in [ms].
 */
#define APP_TX_DUTYCYCLE                            30000

/*!
 * LoRaWAN User application port
 * @note do not use 224. It is reserved for certification
 */
#define LORAWAN_USER_APP_PORT                       2

/*!
 * LoRaWAN Switch class application port
 * @note do not use 224. It is reserved for certification
 */
#define LORAWAN_SWITCH_CLASS_PORT                   3

/*!
 * LoRaWAN default class
 */
#define LORAWAN_DEFAULT_CLASS                       CLASS_A

/*!
 * LoRaWAN default confirm state
 */
#define LORAWAN_DEFAULT_CONFIRMED_MSG_STATE         LORAMAC_HANDLER_CONFIRMED_MSG

/*!
 * LoRaWAN Adaptive Data Rate
 * @note Please note that when ADR is enabled the end-device should be static
 */
#define LORAWAN_ADR_STATE                           LORAMAC_HANDLER_ADR_ON

/*!
 * LoRaWAN Default Data Rate
 * @note Please note that LORAWAN_DEFAULT_DATA_RATE is used only when LORAWAN_ADR_STATE is disabled
 */
#define LORAWAN_DEFAULT_DATA_RATE                   DR_0

/*!
 * LoRaWAN Default Tx output power
 * @note LORAWAN_DEFAULT_TX_POWER must be defined in the [XXXX_MIN_TX_POWER - XXXX_MAX_TX_POWER] range,
         else the end-device uses the XXXX_DEFAULT_TX_POWER value
 */
#define LORAWAN_DEFAULT_TX_POWER                    TX_POWER_0

/*!
 * LoRaWAN default activation type
 */
#define LORAWAN_DEFAULT_ACTIVATION_TYPE             ACTIVATION_TYPE_OTAA

/*!
 * LoRaWAN force rejoin even if the NVM context is restored
 * @note useful only when context management is enabled by CONTEXT_MANAGEMENT_ENABLED
 */
#define LORAWAN_FORCE_REJOIN_AT_BOOT                false

/*!
 * User application data buffer size
 */
#define LORAWAN_APP_DATA_BUFFER_MAX_SIZE            242

/*!
 * Default Unicast ping slots periodicity
 *
 * \remark periodicity is equal to 2^LORAWAN_DEFAULT_PING_SLOT_PERIODICITY seconds
 *         example: 2^4 = 16 seconds. The end-device will open an Rx slot every 16 seconds.
 */
#define LORAWAN_DEFAULT_PING_SLOT_PERIODICITY       4

/*!
 * Default response timeout for class b and class c confirmed
 * downlink frames in milli seconds.
 *
 * The value shall not be smaller than RETRANSMIT_TIMEOUT plus
 * the maximum time on air.
 */
#define LORAWAN_DEFAULT_CLASS_B_C_RESP_TIMEOUT      8000

/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macros -----------------------------------------------------------*/
/* USER CODE BEGIN EM */
//type 0x45:live uid data,0x54:stored uid data, 0x27:status, 0x22:ind
#define LORA_RFID_MSG_TYPE_LIVE_UID 	 0x45
#define LORA_RFID_MSG_TYPE_STORED_UID 	 0x54
#define LORA_RFID_MSG_TYPE_STATUS 		 0x27
#define LORA_RFID_MSG_TYPE_IND			 0x22

/* Sunucudan gelen komut downlink'i (port LORAWAN_USER_APP_PORT):
 *   [0] = 0x02 (LORA_COMMAND_DOWNLINK_TYPE)
 *   [1] = komut ID (bkz LORA_CMD_ID_*)
 *   [2:N] = komuta ozel parametreler
 * bkz docs/eylem-plani.md madde 3/4/5 - genel komut protokolunun ilk adimi. */
#define LORA_COMMAND_DOWNLINK_TYPE		 0x02U

/* Buzzer/LED komutu, 6 byte:
 *   [0]=0x02 [1]=0x01 [2]=hedef(bit0:buzzer,bit1:led) [3]=pattern(0:surekli,1:bip-bip) [4:5]=istenen sure (SANIYE, buyuk-endian)
 * NOT: cihaz istenen sureyi HER ZAMAN MAX_BUZZER_LED_DURATION_MS ile sinirlar -
 * sunucu/yazilim hatasi sonsuz bir bip-bipe yol acamaz. Sure birimi bilerek
 * MS DEGIL SANIYE - "kayip cihaz bulma" gibi dakikalar surebilecek kullanim
 * senaryolari icin 2 byte'lik alanla (ms olsaydi max ~65,5 sn olurdu, saniye
 * olunca max ~18,2 saat) yeterli araligi sagliyor. */
#define LORA_CMD_ID_BUZZER_LED			 0x01U

/* Tarih araligi sorgu komutu, 10 byte:
 *   [0]=0x02 [1]=0x02 [2:5]=start_timestamp (buyuk-endian) [6:9]=end_timestamp (buyuk-endian)
 * Cihaz bu araliga giren TUM kayitlari (SENT/FAILED farketmeksizin -
 * pcb_get_by_timestamp status'a gore filtrelemiyor) DR'ye duyarli, coklu
 * kayit iceren batch'ler halinde LORA_RFID_MSG_TYPE_QUERY_RESULT ile geri
 * raporlar - bkz docs/eylem-plani.md madde 5. */
#define LORA_CMD_ID_QUERY_BY_DATE		 0x02U

/* Tarih araligi sorgusunun sonuc raporu (uplink). Mevcut LIVE_UID
 * (0x45) tipinden BILEREK ayri - bu "yeni bir olay" degil, gecmisten
 * tekrar raporlanan bir kayit; sunucu ikisini farkli yorumlayabilsin.
 *
 * Header (6 byte) + art arda kayit bloklari:
 *   [0]=uplink counter
 *   [1]=0x46
 *   [2]=bu mesajdaki kayit sayisi
 *   [3]=bu ana kadar rapor edilen TOPLAM kayit sayisi (bu sorgu icin)
 *   [4]=bu mesajin batch index'i (0'dan baslar)
 *   [5]=kirpildi mi (0/1) - PCB_TRUNCATED, daha fazla eslesme olabilir
 *
 * Her kayit, kendi kendini sinirlayan (TLV) degisken uzunlukta:
 *   [0]=uuid_uzunluk (4 ya da 7)
 *   [1:N]=uuid (uuid_uzunluk kadar byte)
 *   [N:N+4]=timestamp (buyuk-endian)
 *
 * DR'ye gore batch basina kac kayit sigacagi calisma zamaninda hesaplanir
 * (bkz GetMaxAppPayloadForCurrentDR, lora_app.c) - ADR aktif oldugu icin
 * sabit degildir. 0 eslesme durumunda tek, kayitsiz bir header mesaji
 * ([2]=0,[3]=0) gonderilir - sunucu sessizce beklemesin diye. */
#define LORA_RFID_MSG_TYPE_QUERY_RESULT	 0x46

/* STATUS mesaj araligini uzaktan degistirme komutu, 4 byte:
 *   [0]=0x02 [1]=0x03 [2:4]=carpan (buyuk-endian, uint16)
 * Gercek aralik = carpan * 30 sn. Carpan 2 byte'lik tel formatini kucuk
 * tutmak icin secildi (4 byte ham saniye yerine) - RTC yedek register'i
 * (32-bit) bunun icin bir sinir degil, sadece daha kompakt bir payload.
 *
 * Sinirlar: carpan [1, 2880] araliginda olmali (1=30 sn, 2880=24 saat).
 * Bu aralik disindaki (0 dahil) HER deger REDDEDILIR - kirpma YAPILMAZ,
 * mevcut ayar degismeden kalir, sadece UART'a loglanir (sessiz basarisizlik
 * yerine seffaf red - operatorun "ne istedimse o oldu sandim ama sessizce
 * baska bir degere sabitlendi" seklinde yaniltilmamasi icin).
 *
 * Kalicilik: carpan degeri (saniyeye cevrilmis hali degil, DOGRUDAN carpan)
 * bir RTC yedek register'inda (bkz lora_app.c STATUS_INTERVAL_BKP_REG)
 * saklanir - watchdog/yazilimsal reset'lerde korunur. Acilista register
 * [1,2880] araliginda degilse (ilk acilista fabrika sifiri oldugu icin
 * otomatik gecersiz sayilir - ayri bir "ayarlandi mi" bayragina gerek
 * yok) varsayilan STATUS_INTERVAL_MULTIPLIER_DEFAULT (120 = 1 saat)
 * kullanilir. */
#define LORA_CMD_ID_SET_STATUS_INTERVAL	 0x03U
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Init Lora Application
  */
void LoRaWAN_Init(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /*__LORA_APP_H__*/
