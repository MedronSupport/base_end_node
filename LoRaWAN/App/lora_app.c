/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    lora_app.c
  * @author  MCD Application Team
  * @brief   Application of the LRWAN Middleware
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

/* Includes ------------------------------------------------------------------*/
#include "platform.h"
#include "sys_app.h"
#include "lora_app.h"
#include "stm32_seq.h"
#include "stm32_timer.h"
#include "utilities_def.h"
#include "app_version.h"
#include "lorawan_version.h"
#include "subghz_phy_version.h"
#include "lora_info.h"
#include "LmHandler.h"
#include "adc_if.h"
#include "CayenneLpp.h"
#include "sys_sensors.h"
#include "flash_if.h"

/* USER CODE BEGIN Includes */
#include "subghz.h"
#include "wake_up_button.h"
#include "MFRC522_STM32.h"
#include "stm32_lpm.h"
#include "persistent_circular_buffer.h"
#include "adc_bat_meas.h"
#include "lora_timesync.h"
#include <stdlib.h>
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/**
  * @brief LoRaWAN application version
  */
extern SPI_HandleTypeDef hspi2;
extern  pcb_handle_t eventBuffer;
extern  uint16_t lastRecordId;
/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/**
  * @brief LoRa State Machine states
  */
typedef enum TxEventType_e
{
  /**
    * @brief Appdata Transmission issue based on timer every TxDutyCycleTime
    */
  TX_ON_TIMER,
  /**
    * @brief Appdata Transmission external event plugged on OnSendEvent( )
    */
  TX_ON_EVENT
  /* USER CODE BEGIN TxEventType_t */

  /* USER CODE END TxEventType_t */
} TxEventType_t;

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/**
  * LEDs period value of the timer in ms
  */
#define LED_PERIOD_TIME 500

/**
  * Join switch period value of the timer in ms
  */
#define JOIN_TIME 2000

/*---------------------------------------------------------------------------*/
/*                             LoRaWAN NVM configuration                     */
/*---------------------------------------------------------------------------*/
/**
  * @brief LoRaWAN NVM Flash address
  * @note last 2 sector of a 128kBytes device
  */
#define LORAWAN_NVM_BASE_ADDRESS                    ((void *)0x0803F000UL)

/* USER CODE BEGIN PD */
#define JOIN_RETRY_MAX      3       /* ard arda otomatik deneme say\u0131s\u0131 */
#define JOIN_RETRY_DELAY    3000   /* otomatik denemeler aras\u0131 bekleme, ms */
#define RFID_ACK_TIMEOUT_MS 80000  /* ACK icin azami bekleme, ms - takilirsa flag'i zorla temizler */
#define BUFFERED_ACK_TIMEOUT_MS 80000  /* ACK icin azami bekleme, ms - takilirsa flag'i zorla temizler */
#define RFID_LPM_USER_MASK \
    (1UL << CFG_LPM_RFID_Id)
#define STATUS_LPM_USER_MASK \
    (1UL << CFG_LPM_STATUS_Id)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private function prototypes -----------------------------------------------*/
/**
  * @brief  LoRa End Node send request
  */
static void SendTxData(void);

/**
  * @brief  TX timer callback function
  * @param  context ptr of timer context
  */
static void OnTxTimerEvent(void *context);

/**
  * @brief  join event callback function
  * @param  joinParams status of join
  */
static void OnJoinRequest(LmHandlerJoinParams_t *joinParams);

/**
  * @brief callback when LoRaWAN application has sent a frame
  * @brief  tx event callback function
  * @param  params status of last Tx
  */
static void OnTxData(LmHandlerTxParams_t *params);

/**
  * @brief callback when LoRaWAN application has received a frame
  * @param appData data received in the last Rx
  * @param params status of last Rx
  */
static void OnRxData(LmHandlerAppData_t *appData, LmHandlerRxParams_t *params);

/**
  * @brief callback when LoRaWAN Beacon status is updated
  * @param params status of Last Beacon
  */
static void OnBeaconStatusChange(LmHandlerBeaconParams_t *params);

/**
  * @brief callback when system time has been updated
  */
static void OnSysTimeUpdate(void);

/**
  * @brief callback when LoRaWAN application Class is changed
  * @param deviceClass new class
  */
static void OnClassChange(DeviceClass_t deviceClass);

/**
  * @brief  LoRa store context in Non Volatile Memory
  */
static void StoreContext(void);

/**
  * @brief  stop current LoRa execution to switch into non default Activation mode
  */


/**
  * @brief  Join switch timer callback function
  * @param  context ptr of Join switch context
  */


/**
  * @brief  Notifies the upper layer that the NVM context has changed
  * @param  state Indicates if we are storing (true) or restoring (false) the NVM context
  */
static void OnNvmDataChange(LmHandlerNvmContextStates_t state);

/**
  * @brief  Store the NVM Data context to the Flash
  * @param  nvm ptr on nvm structure
  * @param  nvm_size number of data bytes which were stored
  */
static void OnStoreContextRequest(void *nvm, uint32_t nvm_size);

/**
  * @brief  Restore the NVM Data context from the Flash
  * @param  nvm ptr on nvm structure
  * @param  nvm_size number of data bytes which were restored
  */
static void OnRestoreContextRequest(void *nvm, uint32_t nvm_size);

/**
  * Will be called each time a Radio IRQ is handled by the MAC layer
  *
  */
static void OnMacProcessNotify(void);

/**
  * @brief Change the periodicity of the uplink frames
  * @param periodicity uplink frames period in ms
  * @note Compliance test protocol callbacks
  */
static void OnTxPeriodicityChanged(uint32_t periodicity);

/**
  * @brief Change the confirmation control of the uplink frames
  * @param isTxConfirmed Indicates if the uplink requires an acknowledgement
  * @note Compliance test protocol callbacks
  */
static void OnTxFrameCtrlChanged(LmHandlerMsgTypes_t isTxConfirmed);

/**
  * @brief Change the periodicity of the ping slot frames
  * @param pingSlotPeriodicity ping slot frames period in ms
  * @note Compliance test protocol callbacks
  */
static void OnPingSlotPeriodicityChanged(uint8_t pingSlotPeriodicity);

/**
  * @brief Will be called to reset the system
  * @note Compliance test protocol callbacks
  */
static void OnSystemReset(void);

/* USER CODE BEGIN PFP */
static void TryJoin(void);
static void JoinTimeoutHandler(void);
static void ReadRFIDCard(void);
static void OnRfidReadTimeoutEvent(void *context);
static void RfidPreventStopMode(void);
static void RfidAllowStopMode(void);
static void SendRFID_Data(void);
static void RfidAckTimeoutHandler(void);
static void ResendRfidData(void);
static void StatusMsgPreventStopMode(void);
static void  StatusMsgAllowStopMode(void);
static void OnStatusMessageHandler(void);
static void SendBufferedRfidLogHandler(void);
static void BufferAckTimeoutHandler(void);
/* Yedi ayri "timer ates alinca sadece bir sequencer task'i tetikle"
 * trampolin fonksiyonu (OnJoinRetryTimerEvent, OnJoinTimeoutEvent,
 * OnRfidAckTimeoutEvent, OnRfidAckRetryTimerEvent, OnStatusMsgTimeoutEvent,
 * OnBufferAckTimeoutEvent, OnBufferedDrainDelayEvent) yerine tek, parametrik
 * bir trampolin - bkz USER CODE BEGIN PV icindeki binding sabitleri. */
static void OnTimerFiresSetTask(void *context);
/* USER CODE END PFP */

/* Private variables ---------------------------------------------------------*/
/**
  * @brief LoRaWAN default activation type
  */
static ActivationType_t ActivationType = LORAWAN_DEFAULT_ACTIVATION_TYPE;

/**
  * @brief LoRaWAN force rejoin even if the NVM context is restored
  */
static bool ForceRejoin = LORAWAN_FORCE_REJOIN_AT_BOOT;

/**
  * @brief LoRaWAN handler Callbacks
  */
static LmHandlerCallbacks_t LmHandlerCallbacks =
{
  .GetBatteryLevel =              GetBatteryLevel,
  .GetTemperature =               GetTemperatureLevel,
  .GetUniqueId =                  GetUniqueId,
  .GetDevAddr =                   GetDevAddr,
  .OnRestoreContextRequest =      OnRestoreContextRequest,
  .OnStoreContextRequest =        OnStoreContextRequest,
  .OnMacProcess =                 OnMacProcessNotify,
  .OnNvmDataChange =              OnNvmDataChange,
  .OnJoinRequest =                OnJoinRequest,
  .OnTxData =                     OnTxData,
  .OnRxData =                     OnRxData,
  .OnBeaconStatusChange =         OnBeaconStatusChange,
  .OnSysTimeUpdate =              OnSysTimeUpdate,
  .OnClassChange =                OnClassChange,
  .OnTxPeriodicityChanged =       OnTxPeriodicityChanged,
  .OnTxFrameCtrlChanged =         OnTxFrameCtrlChanged,
  .OnPingSlotPeriodicityChanged = OnPingSlotPeriodicityChanged,
  .OnSystemReset =                OnSystemReset,
};

/**
  * @brief LoRaWAN handler parameters
  */
static LmHandlerParams_t LmHandlerParams =
{
  .ActiveRegion =             ACTIVE_REGION,
  .DefaultClass =             LORAWAN_DEFAULT_CLASS,
  .AdrEnable =                LORAWAN_ADR_STATE,
  .IsTxConfirmed =            LORAWAN_DEFAULT_CONFIRMED_MSG_STATE,
  .TxDatarate =               LORAWAN_DEFAULT_DATA_RATE,
  .TxPower =                  LORAWAN_DEFAULT_TX_POWER,
  .PingSlotPeriodicity =      LORAWAN_DEFAULT_PING_SLOT_PERIODICITY,
  .RxBCTimeout =              LORAWAN_DEFAULT_CLASS_B_C_RESP_TIMEOUT
};

/**
  * @brief Type of Event to generate application Tx
  */
static TxEventType_t EventType = TX_ON_TIMER;

/**
  * @brief Timer to handle the application Tx
  */
//static UTIL_TIMER_Object_t TxTimer;

/**
  * @brief Tx Timer period
  */
static UTIL_TIMER_Time_t TxPeriodicity = APP_TX_DUTYCYCLE;

/**
  * @brief Join Timer period
  */


/* USER CODE BEGIN PV */
/**
  * @brief User application buffer
  */
static UTIL_TIMER_Object_t RfidReadTimeoutTimer;
static UTIL_TIMER_Object_t StatusMessageTimeoutTimer;
static UTIL_TIMER_Object_t RetryStatusTimer;
static UTIL_TIMER_Time_t RFID_TIMEOUT = MFRC_RFID_READ_TIMEOUT;
static UTIL_TIMER_Time_t STATUS_MSG_TIMEOUT = 3600000;
static UTIL_TIMER_Time_t RETRY_STATUS_TIMEOUT= 15000;
/* Buffer'dan bir kayit gonderilip ACK alindiktan sonra, bir sonraki kaydin
 * gonderilmesinden once beklenecek sure. Onceden ACK gelir gelmez hemen bir
 * sonraki kayit ard arda (aralarinda ~1 sn'den az) gonderiliyordu - bu, ACK
 * sorunlarinin ard arda gonderimle iliskili olup olmadigini ayirt etmek ve
 * radyo/duty-cycle ic ice binmesini azaltmak icin araya konan bir bekleme. */
static UTIL_TIMER_Object_t BufferedDrainDelayTimer;
#define BUFFERED_DRAIN_DELAY_MS 10000
static uint8_t AppDataBuffer[LORAWAN_APP_DATA_BUFFER_MAX_SIZE];
/**
  * @brief User application data structure
  */
static LmHandlerAppData_t AppData = { 0, 0, AppDataBuffer };
/**
  * @brief Uplink counter, sent as the first payload byte
  */
static uint8_t UplinkCounter = 0;

static UTIL_TIMER_Object_t JoinRetryTimer;
static uint8_t JoinRetryCount = 0;
static volatile bool join_in_progress = false;
/* GUVENLIK SUBAPI: TryJoin() -> LmHandlerJoin() cagirdiktan sonra OnJoinRequest()
 * callback'i normalde birkac saniye icinde (RX1/RX2 join-accept pencereleri +
 * islem suresi) gelmesi gerekir. Eger cesitli sebeplerle (ornegin ayni anda
 * uzun suren bloklayici bir RFID okumasi sequencer'i mesgul edip LmHandlerProcess'in
 * zamaninda calismasini engellerse) bu callback HIC gelmezse, join_in_progress
 * kalici olarak true kalir ve TryJoin() bir daha asla calismaz - cihaz tamamen
 * rejoin yapamaz hale gelir (gozlemlenen gercek bir arizaydi). Bu timer, callback
 * makul bir surede gelmezse bayragi zorla temizleyip cihazi kurtarir. */
/* Normal kosullarda (RX1~5sn + RX2~6sn + isleme payi) tek bir LmHandlerJoin()
 * cagrisi ~7-8 sn'de sonuclanir. Buna, join sirasinda zaten baslamis olabilecek
 * bir RFID okumasinin (~6 sn, bloklayici) sequencer'i meselgul etme ihtimalini
 * ve genel islem payini (~2 sn) ekleyince gercekci en kotu senaryo ~16 sn'yi
 * buluyor - 35 sn, bunun ~2.2 kati (Nyquist paylı) guvenlik marjidir. */
#define JOIN_TIMEOUT_MS 35000
static UTIL_TIMER_Object_t JoinTimeoutTimer;

volatile bool stop_read_rfid=false;
static bool RfidStopLockActive = false;
static bool StatusStopLockActive = false;
volatile uuid_t uuid_val_rfid={{0},false,0};
volatile lora_sended_msg_status rfid_sendmsg_status=LSMS_TX_ACK_NONE;
volatile bool rfid_data_pending_on_lora=false;
volatile bool buffered_rfid_data_wait_for_ack=false;
volatile bool status_data_pending_on_lora=false;

static uint32_t g_lastSentTimestamp;
static uint8_t  g_lastSentUid[MAXIMUM_LEN_UUID];
static uint8_t  g_lastSentUidLen;
//buffer send algorithym
static uint16_t brf_data_record_id;
static UTIL_TIMER_Object_t RfidAckTimeoutTimer;
static UTIL_TIMER_Object_t BufferAckTimeoutTimer;
/* Dogrudan RFID gonderiminde ACK alinamazsa, kaydi buffer'a dusurmeden once
 * AYNI icerikle (ayni kart verisi, ayni okuma zamani - sadece gonderim anindaki
 * epoch tazelenir) en fazla RFID_ACK_MAX_RETRY kez, aralarinda RFID_ACK_RETRY_DELAY_MS
 * bekleyerek yeniden denenir. Tum denemeler tukenirse mevcut buffer akisina
 * (persistent circular buffer) düşer. */
static UTIL_TIMER_Object_t RfidAckRetryTimer;
#define RFID_ACK_MAX_RETRY 2
#define RFID_ACK_RETRY_DELAY_MS 5000
static uint8_t rfid_ack_retry_count = 0;
/********************ACK UNSUCCESSFULL TRY **************/
static uint8_t un_successfull_ack_response=0;
#define MAX_UNSUCCESS_ACK_COUNT 5

/* Zaman senkronizasyonunun tamami (epoch takibi, status sayaci, tazelik
 * kontrolu) artik lora_timesync kutuphanesinde - bkz external_libs/
 * lora_app_auxilary/{Inc,Src}/lora_timesync.{h,c}. */

/* ---------------------------------------------------------------------
 * ORTAK YARDIMCILAR - dosya genelinde tekrar eden desenleri tekillestirir.
 * ------------------------------------------------------------------- */

/**
  * @brief  32-bit bir degeri buyuk-endian (big-endian) sirayla 4 baytlik
  *         bir tampona yazar. Payload doldururken (epoch, sayaç, timestamp)
  *         dosya boyunca tekrar eden "(v>>24)&0xFF, (v>>16)&0xFF, ..." dort
  *         satirlik deseni tekillestirir.
  * @param  buf   En az 4 bayt yer olan hedef tampon (AppData.Buffer[N] gibi).
  * @param  value Yazilacak 32-bit deger.
  */
static inline void WriteU32BE(uint8_t *buf, uint32_t value)
{
	buf[0] = (uint8_t)((value >> 24) & 0xFF);
	buf[1] = (uint8_t)((value >> 16) & 0xFF);
	buf[2] = (uint8_t)((value >> 8) & 0xFF);
	buf[3] = (uint8_t)(value & 0xFF);
}

/**
  * @brief  Gonderilemeyen (ACK alinamayan) bir RFID okumasini kalici tampona
  *         (persistent circular buffer) FAILED olarak yazar ve senkronize
  *         eder. g_lastSentTimestamp/g_lastSentUid/g_lastSentUidLen'i kullanir
  *         - bu yuzden cagirilmadan once bu degerlerin guncel olmasi sarttir
  *         (SendRFID_Data ve ResendRfidData zaten bunu garanti eder).
  * @note   Dosya genelinde 8 yerde tekrar eden "pcb_add + log + pcb_sync"
  *         blogunu tekillestirir.
  */
static void PersistFailedRfidSend(void)
{
	pcb_result_t bufferResult = pcb_add(&eventBuffer, g_lastSentTimestamp, g_lastSentUid,
			g_lastSentUidLen, PCB_STATUS_FAILED, &lastRecordId);
	if (bufferResult == PCB_OK)
	{
		APP_LOG(TS_ON, VLEVEL_M, "Record added to RAM. ID: %u, Time: %u, Count: %u",
				lastRecordId, g_lastSentTimestamp, (unsigned int)pcb_count(&eventBuffer));
	}
	else
	{
		USER_LOG("Record add error: %d", bufferResult);
	}
	pcb_sync(&eventBuffer);
}

/**
  * @brief  Bir UTIL_TIMER_Object_t'yi belirli bir sequencer task'ina ve
  *         onceliğine baglar. OnTimerFiresSetTask()'a UTIL_TIMER_Create()'in
  *         "Argument" parametresiyle iletilir.
  */
typedef struct
{
	CFG_SEQ_Task_Id_t  TaskId;
	CFG_SEQ_Prio_Id_t  Prio;
} LoraTimerTaskBinding_t;

/* Dosyadaki 7 ayri "timer ates alinca sadece bir sequencer task'i tetikle"
 * trampolin fonksiyonunun (govdesi tek satir UTIL_SEQ_SetTask cagrisindan
 * ibaret) yerini alan tek, parametrik binding tablosu. Her timer, kendi
 * UTIL_TIMER_Create() cagrisinda bu sabitlerden birinin adresini "Argument"
 * olarak gecer - bkz OnTimerFiresSetTask(). */
static const LoraTimerTaskBinding_t kStatusMsgBinding        = { CFG_SEQ_Task_StatusMSGEvent,        CFG_SEQ_Prio_status_1 };
static const LoraTimerTaskBinding_t kRfidAckTimeoutBinding   = { CFG_SEQ_Task_RfidAckTimeoutEvent,    CFG_SEQ_Prio_0 };
static const LoraTimerTaskBinding_t kBufferAckTimeoutBinding = { CFG_SEQ_Task_BufferedAckTimeoutEvent, CFG_SEQ_Prio_0 };
static const LoraTimerTaskBinding_t kBufferedDrainBinding    = { CFG_SEQ_Task_SendBufferedRFIDEvent,  CFG_SEQ_Prio_0 };
static const LoraTimerTaskBinding_t kRfidAckRetryBinding     = { CFG_SEQ_Task_RfidAckRetryEvent,      CFG_SEQ_Prio_0 };
static const LoraTimerTaskBinding_t kJoinRetryBinding        = { CFG_SEQ_Task_LoRaRejoinEvent,        CFG_SEQ_Prio_0 };
static const LoraTimerTaskBinding_t kJoinTimeoutBinding      = { CFG_SEQ_Task_JoinTimeoutEvent,       CFG_SEQ_Prio_0 };

/**
  * @brief  Yukaridaki kStatusMsgBinding/kRfidAckTimeoutBinding/... sabitlerinden
  *         birine bagli bir timer ates aldiginda cagrilir; o binding'in
  *         belirttigi sequencer task'ini belirtilen oncelikle tetikler.
  * @note   NOT: context NULL olamaz - bu fonksiyonu kullanan her
  *         UTIL_TIMER_Create() cagrisi, bindings'lerden birinin adresini
  *         Argument olarak vermelidir.
  */
static void OnTimerFiresSetTask(void *context)
{
	const LoraTimerTaskBinding_t *binding = (const LoraTimerTaskBinding_t *)context;
	UTIL_SEQ_SetTask((1UL << binding->TaskId), binding->Prio);
}
/* USER CODE END PV */

/* Exported functions ---------------------------------------------------------*/
/* USER CODE BEGIN EF */

/* USER CODE END EF */

void LoRaWAN_Init(void)
{
  /* USER CODE BEGIN LoRaWAN_Init_LV */
	  APP_LOG(TS_OFF, VLEVEL_M, "APP_VERSION:        V%X\r\n",
	          (uint8_t)APP_VERSION);

	  /* Get MW LoraWAN info */
	  APP_LOG(TS_OFF, VLEVEL_M, "MW_LORAWAN_VERSION: V%X.%X.%X\r\n",
	          (uint8_t)(LORAWAN_VERSION >> APP_VERSION_MAIN_SHIFT),
	          (uint8_t)(LORAWAN_VERSION >> APP_VERSION_SUB1_SHIFT),
	          (uint8_t)(LORAWAN_VERSION >> APP_VERSION_SUB2_SHIFT));

	  /* Get MW SubGhz_Phy info */
	  APP_LOG(TS_OFF, VLEVEL_M, "MW_RADIO_VERSION:   V%X.%X.%X\r\n",
	          (uint8_t)(SUBGHZ_PHY_VERSION >> APP_VERSION_MAIN_SHIFT),
	          (uint8_t)(SUBGHZ_PHY_VERSION >> APP_VERSION_SUB1_SHIFT),
	          (uint8_t)(SUBGHZ_PHY_VERSION >> APP_VERSION_SUB2_SHIFT));

  /* USER CODE END LoRaWAN_Init_LV */

  /* USER CODE BEGIN LoRaWAN_Init_1 */
  LoraTimeSync_Init();
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_LoRaRejoinEvent), UTIL_SEQ_RFU, TryJoin);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_ReadRFIDEvent), UTIL_SEQ_RFU,ReadRFIDCard);
  UTIL_TIMER_Create(&RfidReadTimeoutTimer, RFID_TIMEOUT, UTIL_TIMER_ONESHOT, OnRfidReadTimeoutEvent, NULL);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SendRFIDEvent), UTIL_SEQ_RFU, SendRFID_Data);
  UTIL_TIMER_Create(&RfidAckTimeoutTimer, RFID_ACK_TIMEOUT_MS, UTIL_TIMER_ONESHOT, OnTimerFiresSetTask, (void *)&kRfidAckTimeoutBinding);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_RfidAckTimeoutEvent), UTIL_SEQ_RFU, RfidAckTimeoutHandler);
  UTIL_TIMER_Create(&RfidAckRetryTimer, RFID_ACK_RETRY_DELAY_MS, UTIL_TIMER_ONESHOT, OnTimerFiresSetTask, (void *)&kRfidAckRetryBinding);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_RfidAckRetryEvent), UTIL_SEQ_RFU, ResendRfidData);

  UTIL_TIMER_Create(&BufferAckTimeoutTimer, BUFFERED_ACK_TIMEOUT_MS, UTIL_TIMER_ONESHOT, OnTimerFiresSetTask, (void *)&kBufferAckTimeoutBinding);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_BufferedAckTimeoutEvent), UTIL_SEQ_RFU, BufferAckTimeoutHandler);

  //Send Buffered RFID Data
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SendBufferedRFIDEvent), UTIL_SEQ_RFU, SendBufferedRfidLogHandler);
  UTIL_TIMER_Create(&BufferedDrainDelayTimer, BUFFERED_DRAIN_DELAY_MS, UTIL_TIMER_ONESHOT, OnTimerFiresSetTask, (void *)&kBufferedDrainBinding);

  //status message timer
  UTIL_TIMER_Create(&StatusMessageTimeoutTimer, STATUS_MSG_TIMEOUT, UTIL_TIMER_PERIODIC, OnTimerFiresSetTask, (void *)&kStatusMsgBinding);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_StatusMSGEvent), UTIL_SEQ_RFU, OnStatusMessageHandler);

  UTIL_TIMER_Create(&RetryStatusTimer, RETRY_STATUS_TIMEOUT, UTIL_TIMER_ONESHOT, OnTimerFiresSetTask, (void *)&kStatusMsgBinding);
  /* USER CODE END LoRaWAN_Init_1 */

  UTIL_TIMER_Create(&JoinRetryTimer, JOIN_RETRY_DELAY, UTIL_TIMER_ONESHOT, OnTimerFiresSetTask, (void *)&kJoinRetryBinding);
  UTIL_TIMER_Create(&JoinTimeoutTimer, JOIN_TIMEOUT_MS, UTIL_TIMER_ONESHOT, OnTimerFiresSetTask, (void *)&kJoinTimeoutBinding);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_JoinTimeoutEvent), UTIL_SEQ_RFU, JoinTimeoutHandler);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_LmHandlerProcess), UTIL_SEQ_RFU, LmHandlerProcess);

  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_LoRaSendOnTxTimerOrButtonEvent), UTIL_SEQ_RFU, SendTxData);
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_LoRaStoreContextEvent), UTIL_SEQ_RFU, StoreContext);

  /* Init Info table used by LmHandler*/
  LoraInfo_Init();

  /* Init the Lora Stack*/
  LmHandlerInit(&LmHandlerCallbacks, APP_VERSION);

  LmHandlerConfigure(&LmHandlerParams);

  /* USER CODE BEGIN LoRaWAN_Init_2 */
  /* USER CODE END LoRaWAN_Init_2 */

  LmHandlerJoin(ActivationType, ForceRejoin);

  if (EventType == TX_ON_TIMER)
  {
  }
  else
  {
    /* USER CODE BEGIN LoRaWAN_Init_3 */

    /* USER CODE END LoRaWAN_Init_3 */
  }

  /* USER CODE BEGIN LoRaWAN_Init_Last */
  UTIL_TIMER_Start(&StatusMessageTimeoutTimer);
  /* USER CODE END LoRaWAN_Init_Last */
}

/* USER CODE BEGIN PB_Callbacks */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	switch (GPIO_Pin) {
	case WakeUpButtonPin:
		static uint32_t lastButtonTick = 0U;
		uint32_t currentTick;

		if (GPIO_Pin != WakeUpButtonPin) {
			return;
		}

		currentTick = HAL_GetTick();


		if ((uint32_t) (currentTick - lastButtonTick) < WAKE_UP_BUTTON_DEBOUNCE_MS) {
			return;
		}


		if (HAL_GPIO_ReadPin(WakeUpButtonPort, WakeUpButtonPin)
				!= GPIO_PIN_RESET) {
			return;
		}

		lastButtonTick = currentTick;
		 APP_LOG(TS_OFF, VLEVEL_M, "Butona basildi....\r\n");
		/* Geçerli buton basma işlemi */
		UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_ReadRFIDEvent),
				CFG_SEQ_Prio_0);
		break;
	default:
		break;
	}
}

/* USER CODE END PB_Callbacks */

/* Private functions ---------------------------------------------------------*/
/* USER CODE BEGIN PrFD */
static void StatusMsgPreventStopMode(void)
{
    if (StatusStopLockActive == false)
    {
        UTIL_LPM_SetStopMode(
                      STATUS_LPM_USER_MASK,
                      UTIL_LPM_DISABLE);

        StatusStopLockActive = true;
    }
}

static void  StatusMsgAllowStopMode(void)
{
    if (StatusStopLockActive == true)
    {
        UTIL_LPM_SetStopMode(
                      STATUS_LPM_USER_MASK,
            UTIL_LPM_ENABLE);

        StatusStopLockActive = false;
    }
}
static void OnStatusMessageHandler(void)
{
	UTIL_TIMER_Stop(&RetryStatusTimer);
	if (LmHandlerJoinStatus() != LORAMAC_HANDLER_SET)
	  {
		  APP_LOG(TS_OFF, VLEVEL_M, "###### Status mesaji icin join yok, gonderim atlandi\r\n");
			JoinRetryCount = 0;
			UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaRejoinEvent),
					CFG_SEQ_Prio_0);
		  return;
	  }


	  if (rfid_data_pending_on_lora||buffered_rfid_data_wait_for_ack)
	  {
		  /* Ayni AppData buffer'ini ve tek seferde bir confirmed uplink kuralini
		   * paylasiyoruz - RFID gonderimi hala ACK bekliyorsa bu status turunu atla,
		   * 15 sn sonra zaten yeniden denenecek. */
		  APP_LOG(TS_OFF, VLEVEL_M, "###### RFID gonderimi ACK bekliyor, status gonderimi atlandi\r\n");
		  UTIL_TIMER_Start(&RetryStatusTimer);
		  return;
	  }
	  StatusMsgPreventStopMode();
	  int16_t bat_temp_q8_8 = 0;
	  uint16_t bat_adc_val = adc_conv_get_battery_volatge(&bat_temp_q8_8);
	  APP_LOG(TS_OFF, VLEVEL_M,"Battery ADC Value:%d mV, Temp:%d.%02d C\r\n",
	          bat_adc_val, bat_temp_q8_8 >> 8, (int)((bat_temp_q8_8 & 0xFF) * 100 / 256));
	  StatusMsgAllowStopMode();




	  /* 14 byte payload:
	   *   [0]     uplink counter
	   *   [1]     type = 0x27 (LORA_RFID_MSG_TYPE_STATUS)
	   *   [2:3]   batarya gerilimi mV, big endian
	   *   [4:5]   sicaklik Q8.8 (deger/256.0 = derece C), big endian
	   *   [6:9]   kacinci status gonderimi (uint32, big endian) - lora_timesync
	   *           kutuphanesi tarafindan yonetilir (her rejoin'de sifirlanir,
	   *           ACK alinamayan bir denemenin retry'i AYNI degeri kullanir).
	   *           Sunucu bunu ayni deger olarak time-sync downlink'inde geri
	   *           gonderiyor, biz de OnRxData'da sadece TAZELIK KONTROLU icin
	   *           kullaniyoruz (esitse kabul, degilse reddet).
	   *   [10:13] cihazin gonderim anindaki guncel epoch tahmini (LoraTimeSync_GetCurrentUnixTime,
	   *           sn, big endian)
	   */
	  uint32_t statusNowEpoch = LoraTimeSync_GetCurrentUnixTime();
	  uint32_t statusCounterToSend = LoraTimeSync_GetCounterForStatusSend();
	  AppData.Port = LORAWAN_USER_APP_PORT;
	  AppData.Buffer[0] = UplinkCounter++;
	  AppData.Buffer[1] = LORA_RFID_MSG_TYPE_STATUS;
	  AppData.Buffer[2] = (uint8_t)((bat_adc_val >> 8) & 0xFF);
	  AppData.Buffer[3] = (uint8_t)(bat_adc_val & 0xFF);
	  AppData.Buffer[4] = (uint8_t)(((uint16_t)bat_temp_q8_8 >> 8) & 0xFF);
	  AppData.Buffer[5] = (uint8_t)((uint16_t)bat_temp_q8_8 & 0xFF);
	  WriteU32BE(&AppData.Buffer[6], statusCounterToSend);
	  WriteU32BE(&AppData.Buffer[10], statusNowEpoch);
	  AppData.BufferSize = 14;

	  status_data_pending_on_lora = true;
	  LmHandlerErrorStatus_t sendStatus = LmHandlerSend(&AppData, LORAMAC_HANDLER_CONFIRMED_MSG, false);
	  if (LORAMAC_HANDLER_SUCCESS == sendStatus)
	  {
		  LoraTimeSync_OnStatusQueued();
		  APP_LOG(TS_OFF, VLEVEL_M, "###### STATUS SEND REQUEST (bat=%d mV, sayac=%u) - ACK bekleniyor\r\n", bat_adc_val, (unsigned int)statusCounterToSend);
	  }
	  else
	  {
		  status_data_pending_on_lora = false;
		  APP_LOG(TS_OFF, VLEVEL_M, "###### STATUS SEND FAILED (%d)\r\n", (int)sendStatus);
	  }


}

static void RfidPreventStopMode(void)
{
    if (RfidStopLockActive == false)
    {
        UTIL_LPM_SetStopMode(
            RFID_LPM_USER_MASK,
			UTIL_LPM_DISABLE);

        RfidStopLockActive = true;
    }
}

static void RfidAllowStopMode(void)
{
    if (RfidStopLockActive == true)
    {
        UTIL_LPM_SetStopMode(
            RFID_LPM_USER_MASK,
            UTIL_LPM_ENABLE);

        RfidStopLockActive = false;
    }
}
static void OnRfidReadTimeoutEvent(void *context)
{
  //UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaSendOnTxTimerOrButtonEvent), CFG_SEQ_Prio_0);
	stop_read_rfid=true;
}
static void RfidAckTimeoutHandler(void)
{
	if (rfid_data_pending_on_lora)
	{
		APP_LOG(TS_OFF, VLEVEL_M, "###### RFID ACK timeout - OnTxData gelmedi, veri depoya yaziliyor\r\n");
		PersistFailedRfidSend();
		rfid_data_pending_on_lora = false;
	}
}

static void BufferAckTimeoutHandler(void)
{
	if (buffered_rfid_data_wait_for_ack)
	{
		APP_LOG(TS_OFF, VLEVEL_M, "###### Buffer ACK timeout - OnTxData gelmedi\r\n");

		buffered_rfid_data_wait_for_ack=false;
	}
}
static void SendBufferedRfidLogHandler(void) {

    if (rfid_data_pending_on_lora || status_data_pending_on_lora) {
            APP_LOG(TS_OFF, VLEVEL_M, "###### Baska bir gonderim ACK bekliyor, buffer denemesi ertelendi\r\n");
            return;
    }
	APP_LOG(TS_OFF, VLEVEL_M, "SendBufferedRfidLogHandler \r\n");

	pcb_record_t unsended_record;
	pcb_result_t last_failed_data_fetch_result = pcb_get_latest_by_status(&eventBuffer,PCB_STATUS_FAILED,&unsended_record);

	if (last_failed_data_fetch_result == PCB_OK) {



		APP_LOG(TS_ON, VLEVEL_M,"RFID LOG: recordid: %d, timestamp id:%u,len:%d\r\n",unsended_record.record_id,(unsigned int)unsended_record.timestamp,unsended_record.uuid_length);
		if (unsended_record.uuid_length == 7) {
			APP_LOG(TS_OFF, VLEVEL_M, "\t\t\t CARD ID:%02X %02X %02X %02X %02X %02X %02X\r\n",
					unsended_record.uuid[0], unsended_record.uuid[1], unsended_record.uuid[2], unsended_record.uuid[3], unsended_record.uuid[4], unsended_record.uuid[5], unsended_record.uuid[6]);
		} else if (unsended_record.uuid_length == 4) {

			APP_LOG(TS_OFF, VLEVEL_M, "\t\t\t CARD ID:%02X %02X %02X %02X\r\n", unsended_record.uuid[0], unsended_record.uuid[1], unsended_record.uuid[2], unsended_record.uuid[3]);
		}

		brf_data_record_id=unsended_record.record_id;

		LmHandlerErrorStatus_t status = LORAMAC_HANDLER_ERROR;
		/* 18 byte payload:
		 *
		 *   [0]   	uplink counter
		 *   [1]		type 0x45:live uid data,0x54:stored uid data, 0x27:status, 0x22:ind
		 *   [2:5] 	kaydin timestamp'i (kart okunduğu an), 4 byte big endian
		 *   [6]   	uid length
		 *   [7:13]   uid - 7 byte big endian
		 *   [14:17]  cihazin GONDERIM anindaki guncel epoch tahmini (LoraTimeSync_GetCurrentUnixTime,
		 *            sn, big endian) - bu bir RETRY oldugu icin [2:5]'ten farkli
		 *            olabilir; sunucu tarafinda time-sync tracker icin kullanilabilir.
		 */
		uint32_t bufferedNowEpoch = LoraTimeSync_GetCurrentUnixTime();

		AppData.Port = LORAWAN_USER_APP_PORT;
		AppData.Buffer[0] = UplinkCounter++;
		AppData.Buffer[1] = LORA_RFID_MSG_TYPE_LIVE_UID;
		WriteU32BE(&AppData.Buffer[2], (uint32_t)unsended_record.timestamp);

		AppData.Buffer[6] = unsended_record.uuid_length;

		for(int j=0;j<MAXIMUM_LEN_UUID;j++)
		{
			if(j<unsended_record.uuid_length)
			{
				AppData.Buffer[7+j] = unsended_record.uuid[j];
			}else{
				AppData.Buffer[7+j] = 0x00;
			}
		}
		WriteU32BE(&AppData.Buffer[14], bufferedNowEpoch);
		AppData.BufferSize = 18;
		UTIL_TIMER_Time_t nextTxIn = 0;


		status = LmHandlerSend(&AppData, LmHandlerParams.IsTxConfirmed, false);
		if (LORAMAC_HANDLER_SUCCESS == status)
		{
			APP_LOG(TS_ON, VLEVEL_L, "SEND REQUEST (port %d, %d bytes)\r\n",
					AppData.Port, AppData.BufferSize);
			buffered_rfid_data_wait_for_ack=true;
			UTIL_TIMER_Start(&BufferAckTimeoutTimer);

		}
		else if (LORAMAC_HANDLER_DUTYCYCLE_RESTRICTED == status)
		{
			nextTxIn = LmHandlerGetDutyCycleWaitTime();
			if (nextTxIn > 0)
			{
				APP_LOG(TS_ON, VLEVEL_M, "Next Tx in  : ~%d second(s)\r\n", (nextTxIn / 1000));
			}
			buffered_rfid_data_wait_for_ack=false;

		}
		else
		{
			APP_LOG(TS_ON, VLEVEL_M, "SEND FAILED (%d)\r\n", (int)status);
			buffered_rfid_data_wait_for_ack=false;

		}




}else if(last_failed_data_fetch_result == PCB_ERROR_EMPTY){
	//Burada artık bufferda gönderilmemiş veri kalmadığını anlıyoruz.
	APP_LOG(TS_ON, VLEVEL_L, "BUFFER IS EMPTY\r\n");
}else if(last_failed_data_fetch_result == PCB_ERROR_NOT_FOUND)
{
	APP_LOG(TS_ON, VLEVEL_L, "ALL DATA STATUS IS SENT\r\n");
}

}

static void SendRFID_Data(void) {
	APP_LOG(TS_OFF, VLEVEL_M, "###### Send RFID has triggered... \r\n");
	if (uuid_val_rfid.is_uuid_data_assigned) {

		  uint32_t timestamp=LoraTimeSync_GetCurrentUnixTime();
		APP_LOG(TS_OFF, VLEVEL_M, "###### New uuid to send \r\n");

		  g_lastSentTimestamp = timestamp;
		  memcpy(g_lastSentUid, (const void *)uuid_val_rfid.uid, MAXIMUM_LEN_UUID);
		  g_lastSentUidLen = uuid_val_rfid.uuid_len;

          if (buffered_rfid_data_wait_for_ack || status_data_pending_on_lora) {
               PersistFailedRfidSend();
               return;
           }

		if (LmHandlerJoinStatus() != LORAMAC_HANDLER_SET) {
			APP_LOG(TS_ON, VLEVEL_L,
					"Henuz join olunmadi, gonderim yerine rejoin deneniyor\r\n");
			JoinRetryCount = 0;
			UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaRejoinEvent),
					CFG_SEQ_Prio_0);

			  PersistFailedRfidSend();
			return;
		}
		LmHandlerErrorStatus_t status = LORAMAC_HANDLER_ERROR;
		  /* 18 byte payload:
		   *
		   *   [0]   	uplink counter
		   *   [1]		type 0x45:live uid data,0x54:stored uid data, 0x27:status, 0x22:ind
		   *   [2:5] 	timestamp 4 byte big endian (kart okundugu an)
		   *   [6]   	uid length
		   *   [7:13]   uid - 7 byte  big endian
		   *   [14:17]  cihazin GONDERIM anindaki guncel epoch tahmini (LoraTimeSync_GetCurrentUnixTime,
		   *            sn, big endian) - canli gonderimde [2:5] ile pratikte ayni/yakin
		   *            olur, ama diger mesaj tiplerinde de ayni sabit konumda bu alan
		   *            bulunsun diye tutarlilik icin ekleniyor.
		   */
		  uint32_t liveNowEpoch = LoraTimeSync_GetCurrentUnixTime();

		  AppData.Port = LORAWAN_USER_APP_PORT;
		  AppData.Buffer[0] = UplinkCounter++;
		  AppData.Buffer[1] = LORA_RFID_MSG_TYPE_LIVE_UID;
		  WriteU32BE(&AppData.Buffer[2], (uint32_t)timestamp);

		  AppData.Buffer[6] = uuid_val_rfid.uuid_len;

		  for(int i=0;i<MAXIMUM_LEN_UUID;i++)
		  {
			  if(i<uuid_val_rfid.uuid_len)
			  {
				  AppData.Buffer[7+i] = uuid_val_rfid.uid[i];
			  }else{
				  AppData.Buffer[7+i] = 0;
			  }
		  }
		  WriteU32BE(&AppData.Buffer[14], liveNowEpoch);
		  AppData.BufferSize = 18;
		  UTIL_TIMER_Time_t nextTxIn = 0;



		  status = LmHandlerSend(&AppData, LmHandlerParams.IsTxConfirmed, false);
		  if (LORAMAC_HANDLER_SUCCESS == status)
		  {
		    APP_LOG(TS_ON, VLEVEL_L, "SEND REQUEST (port %d, %d bytes)\r\n",
		            AppData.Port, AppData.BufferSize);

		    rfid_data_pending_on_lora = true;
		    UTIL_TIMER_Start(&RfidAckTimeoutTimer);
		   // HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
		  }
		  else if (LORAMAC_HANDLER_DUTYCYCLE_RESTRICTED == status)
		  {
		    nextTxIn = LmHandlerGetDutyCycleWaitTime();
		    if (nextTxIn > 0)
		    {
		      APP_LOG(TS_ON, VLEVEL_M, "Next Tx in  : ~%d second(s)\r\n", (nextTxIn / 1000));
		    }
		    rfid_data_pending_on_lora = false;
			  PersistFailedRfidSend();
		  }
		  else
		  {
			  APP_LOG(TS_ON, VLEVEL_M, "SEND FAILED (%d)\r\n", (int)status);
			  PersistFailedRfidSend();
			  rfid_data_pending_on_lora=false;
		  }

	}else{

		if (LmHandlerJoinStatus() != LORAMAC_HANDLER_SET) {
			APP_LOG(TS_ON, VLEVEL_M,
					"Henuz join olunmadi, gonderim yerine rejoin deneniyor\r\n");
			JoinRetryCount = 0;
			UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaRejoinEvent),
					CFG_SEQ_Prio_0);
			return;
		}
		APP_LOG(TS_OFF, VLEVEL_M, "###### Kart okunamadi, buffer kontrol ediliyor\r\n");
		UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SendBufferedRFIDEvent), CFG_SEQ_Prio_0);
	}
}

/* SendRFID_Data() icinde ACK alinamayan bir denemenin, buffer'a dusurulmeden
 * once RFID_ACK_MAX_RETRY kez tekrar denenmesi icin kullanilir. g_lastSentUid/
 * g_lastSentUidLen/g_lastSentTimestamp - basarisiz olan denemede zaten
 * doldurulmus olan onbellek degerleri - AYNEN kullanilir (kart yeniden
 * okunmaz), sadece [14:17] "gonderim anindaki epoch" alani bu denemenin
 * gercek zamanina gore tazelenir. */
static void ResendRfidData(void)
{
	if (LmHandlerJoinStatus() != LORAMAC_HANDLER_SET) {
		APP_LOG(TS_OFF, VLEVEL_M, "###### RFID retry: join yok, depoya yaziliyor\r\n");
		rfid_ack_retry_count = 0;
		PersistFailedRfidSend();
		rfid_data_pending_on_lora = false;
		JoinRetryCount = 0;
		UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaRejoinEvent), CFG_SEQ_Prio_0);
		return;
	}

	uint32_t liveNowEpoch = LoraTimeSync_GetCurrentUnixTime();
	LmHandlerErrorStatus_t status;

	AppData.Port = LORAWAN_USER_APP_PORT;
	AppData.Buffer[0] = UplinkCounter++;
	AppData.Buffer[1] = LORA_RFID_MSG_TYPE_LIVE_UID;
	WriteU32BE(&AppData.Buffer[2], g_lastSentTimestamp);
	AppData.Buffer[6] = g_lastSentUidLen;
	for (int i = 0; i < MAXIMUM_LEN_UUID; i++)
	{
		AppData.Buffer[7 + i] = (i < g_lastSentUidLen) ? g_lastSentUid[i] : 0x00;
	}
	WriteU32BE(&AppData.Buffer[14], liveNowEpoch);
	AppData.BufferSize = 18;

	status = LmHandlerSend(&AppData, LmHandlerParams.IsTxConfirmed, false);
	if (LORAMAC_HANDLER_SUCCESS == status)
	{
		APP_LOG(TS_OFF, VLEVEL_M, "###### RFID RETRY GONDERILDI (deneme %u/%u) - ACK bekleniyor\r\n",
				rfid_ack_retry_count, RFID_ACK_MAX_RETRY);
		rfid_data_pending_on_lora = true;
		UTIL_TIMER_Start(&RfidAckTimeoutTimer);
	}
	else
	{
		/* Kuyruga bile alinamadi (MAC busy/duty-cycle/vs) - bu denemeyi
		 * kaybetmis sayip dogrudan depoya yaz, kalan retry haklarini
		 * bosa harcama. */
		APP_LOG(TS_OFF, VLEVEL_M, "###### RFID RETRY GONDERILEMEDI (%d), depoya yaziliyor\r\n", (int)status);
		rfid_ack_retry_count = 0;

		PersistFailedRfidSend();
		rfid_data_pending_on_lora = false;
	}
}

static void ReadRFIDCard(void) {
	if (rfid_data_pending_on_lora) {
		APP_LOG(TS_OFF, VLEVEL_M, "###### Onceki RFID gonderimi hala ACK bekliyor, yeni okuma reddedildi\r\n");
		return;
	}
	if (join_in_progress) {
		/* Aktif bir join surerken RFID okumasi baslatmiyoruz - okuma ~5-6 sn
		 * bloklayan bir dongu (HAL_Delay tabanli) ve sequencer'i mesgul ederek
		 * tam da join-accept RX1/RX2 pencerelerinin islenmesini geciktirebilir
		 * (gozlemlenmis gercek bir ariza senaryosu). Join bitince (basarili/
		 * basarisiz, ya da timeout guard'i devreye girince) buton tekrar
		 * calisir hale gelir. */
		APP_LOG(TS_OFF, VLEVEL_M, "###### Join surüyor, RFID okumasi ertelendi\r\n");
		return;
	}
	awake_led_gpio_init();
	APP_LOG(TS_OFF, VLEVEL_M, "###### Read RFID has triggered... \r\n");
	//uint8_t uid[7];
	uuid_val_rfid.is_uuid_data_assigned=false;
	memset(uuid_val_rfid.uid,0,MAXIMUM_LEN_UUID);
	MFRC522_t rfID = { &hspi2, CS_GPIO_Port, CS_Pin, SPI_RESET_GPIO_Port,
			SPI_RESET_Pin };
	RfidPreventStopMode();
	BuzzerNotify_init();
	MFRC522_Power_On_By_GPIO();
	HAL_Delay(5);
	stop_read_rfid = false;
	rfid_read_process_init(&rfID);
	APP_LOG(TS_OFF, VLEVEL_M, "rfid_read_process_init \r\n");
	UTIL_TIMER_Start(&RfidReadTimeoutTimer);
	APP_LOG(TS_OFF, VLEVEL_M, "RfidReadTimeoutTimer \r\n");
	for(int i=0;i<5;i++)
	{
		awake_led_gpio_toggle();
		HAL_Delay(100);
	}

	while (!stop_read_rfid) {
		APP_LOG(TS_OFF, VLEVEL_M, "loop \r\n");
		uint8_t len = 0;
		if (waitcardDetectUntilTimeout(&rfID, MFRC_RFID_WAIT_DETECT_TIMEOUT) == STATUS_OK) {
			APP_LOG(TS_OFF, VLEVEL_M, "###### Card Detected... \r\n");
			if (MFRC522_ReadUid(&rfID, uuid_val_rfid.uid, &len) == STATUS_OK) {
				USER_LOG("len %d", len);
				uuid_val_rfid.uuid_len=len;
				if (len == 7) {
				APP_LOG(TS_OFF, VLEVEL_M, "CARD ID:%02X %02X %02X %02X %02X %02X %02X\r\n",
						uuid_val_rfid.uid[0], uuid_val_rfid.uid[1], uuid_val_rfid.uid[2], uuid_val_rfid.uid[3], uuid_val_rfid.uid[4], uuid_val_rfid.uid[5],uuid_val_rfid.uid[6]);
				} else if (len == 4) {

					APP_LOG(TS_OFF, VLEVEL_M, "CARD ID:%02X %02X %02X %02X\r\n", uuid_val_rfid.uid[0], uuid_val_rfid.uid[1],
							uuid_val_rfid.uid[2], uuid_val_rfid.uid[3]);
				}
				Buzzer_Alert_Process(1500);
				uuid_val_rfid.is_uuid_data_assigned=true;
				break;
			}

		}
		awake_led_gpio_toggle();
		waitcardRemovalUntilTimeout(&rfID,MFRC_RFID_WAIT_REMOVE_TIMEOUT);
	}
	awake_led_gpio_deinit();
	BuzzerNotify_deinit();
	UTIL_TIMER_Stop(&RfidReadTimeoutTimer);
	MFRC522_Hardware_Reset(&rfID);
	MFRC522_Spi_Deinit();
	HAL_Delay(5);
	MFRC522_Power_Reset_By_GPIO();
	UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SendRFIDEvent),
			CFG_SEQ_Prio_0);
	RfidAllowStopMode();
	APP_LOG(TS_OFF, VLEVEL_M, "RFID Read task has ending...\r\n");
}
static void TryJoin(void)
{
  if (join_in_progress)
  {
    APP_LOG(TS_OFF, VLEVEL_M, "###### TryJoin zaten surüyor, atlandi\r\n");
    return;
  }
  join_in_progress = true;

  APP_LOG(TS_OFF, VLEVEL_M, "###### TryJoin() tetiklendi - zorla rejoin\r\n");
 // UTIL_TIMER_Stop(&TxTimer);

  if (LORAMAC_HANDLER_SUCCESS != LmHandlerStop())
  {
    APP_LOG(TS_OFF, VLEVEL_M, "LmHandler Stop on going ...\r\n");
    join_in_progress = false;
    return;
  }

  LmHandlerConfigure(&LmHandlerParams);
  LmHandlerJoin(ActivationType, true);   /* ActivationType  */
  UTIL_TIMER_Start(&JoinTimeoutTimer);

}

static void JoinTimeoutHandler(void)
{
  /* OnJoinRequest() beklenen surede (JOIN_TIMEOUT_MS) gelmediyse join_in_progress
   * kalici olarak takili kalir ve TryJoin() bir daha asla calismaz - bu, cihazin
   * kalici olarak rejoin yapamaz hale gelmesine yol acan gercek bir ariza senaryosuydu.
   * Guvenlik supabi: bayragi zorla temizleyip cihazi kurtariyoruz, bir sonraki
   * rejoin denemesi (status/RFID/timer tetikli) normal sekilde calisabilir. */
  if (join_in_progress)
  {
    APP_LOG(TS_OFF, VLEVEL_M, "###### JOIN TIMEOUT - OnJoinRequest gelmedi, guard zorla temizleniyor\r\n");
    join_in_progress = false;
  }
}
/* USER CODE END PrFD */

static void OnRxData(LmHandlerAppData_t *appData, LmHandlerRxParams_t *params)
{
  /* USER CODE BEGIN OnRxData_1 */
	  if ((appData != NULL) && (params != NULL))
	  {
	    APP_LOG(TS_OFF, VLEVEL_M, "###### D/L FRAME: port %d, %d byte(s), RSSI %d, SNR %d\r\n",
	            appData->Port, appData->BufferSize, params->Rssi, params->Snr);

	    if ((appData->BufferSize > 0) && (appData->Buffer != NULL))
	    {
	    	APP_LOG(TS_OFF, VLEVEL_M, "###### D/L mesaj tipi: 0x%02X, cihazin bildigi guncel epoch: %u\r\n",
	    			appData->Buffer[0], (unsigned int)LoraTimeSync_GetCurrentUnixTime());
	    }

	    /* Zaman senkronizasyon mesajinin parse/tazelik-kontrolu/uygulanmasi
	     * artik lora_timesync kutuphanesinde - burada sadece fPort filtresi
	     * yapip sonucu cagiriyoruz. */
	    if (appData->Port == LORAWAN_USER_APP_PORT)
	    {
	    	if (LoraTimeSync_HandleDownlink(appData->Buffer, (uint8_t)appData->BufferSize))
	    	{
	    		APP_LOG(TS_OFF, VLEVEL_M, "###### TIME SYNC alindi: epoch=%u\r\n",
	    				(unsigned int)LoraTimeSync_GetCurrentUnixTime());
	    	}
	    }
	  }
  /* USER CODE END OnRxData_1 */
}

static void SendTxData(void)
{
  /* USER CODE BEGIN SendTxData_1 */
	  if (LmHandlerJoinStatus() != LORAMAC_HANDLER_SET)
	  {
	    APP_LOG(TS_ON, VLEVEL_L, "Henuz join olunmadi, gonderim yerine rejoin deneniyor\r\n");
	    JoinRetryCount = 0;
	    UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaRejoinEvent), CFG_SEQ_Prio_0);
	    return;
	  }
	 LmHandlerErrorStatus_t status = LORAMAC_HANDLER_ERROR;
		  uint16_t voltage = SYS_GetBatteryLevel();           /* mV                      */
		  int16_t temperature = SYS_GetTemperatureLevel();    /* signed Q8.8, in degC    */
		  UTIL_TIMER_Time_t nextTxIn = 0;

		  /* 6 byte payload:
		   *   [0]   uplink counter
		   *   [1:2] supply voltage in mV, big endian
		   *   [3:4] temperature as signed Q8.8 (value / 256.0 = degC), big endian
		   *   [5]   current LED state
		   */
		  AppData.Port = LORAWAN_USER_APP_PORT;
		  AppData.Buffer[0] = UplinkCounter++;
		  AppData.Buffer[1] = (uint8_t)((voltage >> 8) & 0xFF);
		  AppData.Buffer[2] = (uint8_t)(voltage & 0xFF);
		  AppData.Buffer[3] = (uint8_t)(((uint16_t)temperature >> 8) & 0xFF);
		  AppData.Buffer[4] = (uint8_t)((uint16_t)temperature & 0xFF);
		  AppData.Buffer[5] = (uint8_t)1;
		  AppData.BufferSize = 6;

		  status = LmHandlerSend(&AppData, LmHandlerParams.IsTxConfirmed, false);
		  if (LORAMAC_HANDLER_SUCCESS == status)
		  {
		    APP_LOG(TS_ON, VLEVEL_L, "SEND REQUEST (port %d, %d bytes)\r\n",
		            AppData.Port, AppData.BufferSize);

		  }
		  else if (LORAMAC_HANDLER_DUTYCYCLE_RESTRICTED == status)
		  {
		    nextTxIn = LmHandlerGetDutyCycleWaitTime();
		    if (nextTxIn > 0)
		    {
		      APP_LOG(TS_ON, VLEVEL_L, "Next Tx in  : ~%d second(s)\r\n", (nextTxIn / 1000));
		    }
		  }
		  else
		  {
		    APP_LOG(TS_ON, VLEVEL_L, "SEND FAILED (%d)\r\n", (int)status);

		  }
  /* USER CODE END SendTxData_1 */
}

static void OnTxTimerEvent(void *context)
{
  /* USER CODE BEGIN OnTxTimerEvent_1 */

  /* USER CODE END OnTxTimerEvent_1 */
 // UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaSendOnTxTimerOrButtonEvent), CFG_SEQ_Prio_0);

  /*Wait for next tx slot*/
  //UTIL_TIMER_Start(&TxTimer);
  /* USER CODE BEGIN OnTxTimerEvent_2 */

  /* USER CODE END OnTxTimerEvent_2 */
}

/* USER CODE BEGIN PrFD_LedEvents */

/* USER CODE END PrFD_LedEvents */

static void OnTxData(LmHandlerTxParams_t *params)
{
	//LmHandlerGetCurrentDatarate()
	int8_t data_rate;
	LmHandlerGetTxDatarate(&data_rate);
	APP_LOG(TS_OFF, VLEVEL_M, "Tx Data Rate: %d\r\n",data_rate);

	/* KRITIK: MlmeConfirm() (LmHandler.c) bu callback'i join/link-check gibi
	 * MLME confirm'leri icin de cagiriyor, ama TxParams.MsgType/AckReceived/
	 * UplinkCounter alanlarina DOKUNMUYOR - yani join basarisiz oldugunda bu
	 * alanlar en son gercek veri mesajindan (McpsConfirm) kalma BAYAT degerleri
	 * tasiyor. IsMcpsConfirm==0 olan (yani MLME/join kaynakli) cagrilari burada
	 * eleyip yalnizca gercek veri mesaji confirm'lerini (IsMcpsConfirm==1)
	 * isliyoruz - aksi halde her join hatasi, son gonderilen veri mesaji
	 * confirmed'sa "CONFIRMED TX BASARISIZ" sanilip un_successfull_ack_response
	 * sayacini artiriyor ve esik asilinca JoinRetryCount'u sifirlayip
	 * JOIN_RETRY_MAX'in "3 deneme sonra dur" mantigini bozuyordu (gozlemlenen
	 * gercek bir ariza - log'larda ayni "uplink #N" sayaciyla onlarca kez
	 * tekrar eden sahte ACK hatasi olarak goruldu). */
	if ((params != NULL) && (params->IsMcpsConfirm != 0) && (params->MsgType == LORAMAC_HANDLER_CONFIRMED_MSG))
	{
		if (params->AckReceived == 0)
		{


			APP_LOG(TS_OFF, VLEVEL_M, "###### CONFIRMED TX BASARISIZ - ACK ALINAMADI (uplink #%d, status=%d)\r\n",
					(int)params->UplinkCounter, (int)params->Status);

			if (status_data_pending_on_lora)
			{
				APP_LOG(TS_OFF, VLEVEL_M, "###### STATUS (batarya) ACK ALINAMADI (uplink #%d), %u sn sonra AYNI sayacla tekrar denenecek\r\n",
						(int)params->UplinkCounter, (unsigned int)(RETRY_STATUS_TIMEOUT / 1000U));
				status_data_pending_on_lora = false;
				LoraTimeSync_OnStatusAckResult(false);
				UTIL_TIMER_Start(&RetryStatusTimer);
			}

			if(rfid_data_pending_on_lora)
			{
				UTIL_TIMER_Stop(&RfidAckTimeoutTimer);
				if (rfid_ack_retry_count < RFID_ACK_MAX_RETRY)
				{
					rfid_ack_retry_count++;
					APP_LOG(TS_OFF, VLEVEL_M, "###### RFID ACK ALINAMADI, %u sn sonra AYNI icerikle tekrar denenecek (deneme %u/%u)\r\n",
							(unsigned int)(RFID_ACK_RETRY_DELAY_MS / 1000U), rfid_ack_retry_count, RFID_ACK_MAX_RETRY);
					/* rfid_data_pending_on_lora KASITLI true birakiliyor - yeni RFID
					 * okumasi veya status gonderimi retry'lar bitene kadar ertelensin. */
					UTIL_TIMER_Start(&RfidAckRetryTimer);
				}
				else
				{
					APP_LOG(TS_OFF, VLEVEL_M, "###### RFID ACK ALINAMADI (%u/%u deneme tukendi), depoya yaziliyor\r\n",
							rfid_ack_retry_count, RFID_ACK_MAX_RETRY);
					rfid_ack_retry_count = 0;

					PersistFailedRfidSend();
					rfid_data_pending_on_lora=false;
				}
			}

			if(buffered_rfid_data_wait_for_ack)
			{
				buffered_rfid_data_wait_for_ack=false;
				//Gönderilemeyen mesaj için bir şey yapılmayacak zaten bufferdan alındı



			}
			un_successfull_ack_response++;
			if(un_successfull_ack_response>=MAX_UNSUCCESS_ACK_COUNT)
			{
				un_successfull_ack_response=0;
				JoinRetryCount = 0;
				UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LoRaRejoinEvent),
						CFG_SEQ_Prio_0);
			}
		}
		else
		{
			un_successfull_ack_response=0;
			APP_LOG(TS_OFF, VLEVEL_M, "CONFIRMED TX: ACK alindi (uplink #%d)\r\n",
					(int)params->UplinkCounter);
			if(buffered_rfid_data_wait_for_ack)
			{
				UTIL_TIMER_Stop(&BufferAckTimeoutTimer);
				buffered_rfid_data_wait_for_ack=false;

				pcb_result_t set_ended_id_result;
				uint8_t pcb_set_try_count=0;
				do{
					pcb_set_try_count++;
					set_ended_id_result=pcb_set_status_by_id(&eventBuffer,brf_data_record_id,PCB_STATUS_SENT);
				}while((set_ended_id_result!=PCB_OK)&&(pcb_set_try_count<5));

				pcb_result_t pcb_sync_result;
				uint8_t pcb_sync_try_count=0;

				if(set_ended_id_result==PCB_OK)
				{
					do{
						pcb_sync_try_count++;
						pcb_sync_result=pcb_sync(&eventBuffer);;
					}while((pcb_sync_result!=PCB_OK)&&(pcb_sync_try_count<1));
				}
				if((set_ended_id_result==PCB_OK)&&(pcb_sync_result==PCB_OK))
				{
					/* Bir sonraki buffer kaydini hemen degil, BUFFERED_DRAIN_DELAY_MS
					 * (10 sn) sonra gonder - ard arda (aralarinda ~1 sn'den az)
					 * gonderimin ACK sorunlarina katkisi olup olmadigini ayirt
					 * etmek icin araya konan bilincli bekleme. */
					UTIL_TIMER_Start(&BufferedDrainDelayTimer);
				}else{
					APP_LOG(TS_OFF, VLEVEL_M, "###### FLASH SENKTON HATASI!!!! Huston We a Problem \r\n");
				}
			}
			if (status_data_pending_on_lora)
			{
				APP_LOG(TS_OFF, VLEVEL_M, "###### STATUS (batarya) ACK ALINDI (uplink #%d)\r\n",
						(int)params->UplinkCounter);
				status_data_pending_on_lora = false;
				LoraTimeSync_OnStatusAckResult(true);
				UTIL_TIMER_Start(&BufferedDrainDelayTimer);
			}

			if (rfid_data_pending_on_lora) {
				UTIL_TIMER_Stop(&RfidAckTimeoutTimer);
				rfid_ack_retry_count = 0;
				//Buffer send task tetiklenecek
				UTIL_TIMER_Start(&BufferedDrainDelayTimer);
			}
			rfid_data_pending_on_lora=false;
		}
	}
}

static void OnJoinRequest(LmHandlerJoinParams_t *joinParams) {
	/* USER CODE BEGIN OnJoinRequest_1 */
	if (joinParams != NULL) {
		join_in_progress = false;
		UTIL_TIMER_Stop(&JoinTimeoutTimer);
		if (joinParams->Status == LORAMAC_HANDLER_SUCCESS) {
			JoinRetryCount = 0;
			/* Sunucu join event'inde time-sync downlink'ini HER ZAMAN
			 * status_count=0 varsayimiyla gonderiyor - bu varsayimin
			 * gecerli kalmasi icin sayaci burada sifirliyoruz. */
			LoraTimeSync_OnJoined();
			rfid_ack_retry_count = 0;
			APP_LOG(TS_OFF, VLEVEL_M, "\r\n###### = JOINED = %s ======\r\n",
					(joinParams->Mode == ACTIVATION_TYPE_ABP) ? "ABP " : "OTAA");
			UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_StatusMSGEvent), CFG_SEQ_Prio_status_1);
		} else {
			APP_LOG(TS_OFF, VLEVEL_M, "\r\n###### = JOIN FAILED\r\n");
			if (JoinRetryCount < JOIN_RETRY_MAX) // <-- buradan itibaren yeni eklenen kısım
			{
				JoinRetryCount++;
				APP_LOG(TS_OFF, VLEVEL_M,
						"Rejoin deneme %d/%d, %d sn sonra\r\n", JoinRetryCount,
						JOIN_RETRY_MAX, JOIN_RETRY_DELAY / 1000);
				UTIL_TIMER_Start(&JoinRetryTimer); // 10 sn sonra otomatik rejoin tetikler
			} else {
				APP_LOG(TS_OFF, VLEVEL_M,
						"Rejoin denemeleri tukendi, buton veya sonraki dongu bekleniyor\r\n");
			}
		}
	}
	/* USER CODE END OnJoinRequest_1 */
}

static void OnBeaconStatusChange(LmHandlerBeaconParams_t *params)
{
  /* USER CODE BEGIN OnBeaconStatusChange_1 */
  /* USER CODE END OnBeaconStatusChange_1 */
}

static void OnSysTimeUpdate(void)
{
  /* USER CODE BEGIN OnSysTimeUpdate_1 */

  /* USER CODE END OnSysTimeUpdate_1 */
}

static void OnClassChange(DeviceClass_t deviceClass)
{
  /* USER CODE BEGIN OnClassChange_1 */
  /* USER CODE END OnClassChange_1 */
}

static void OnMacProcessNotify(void)
{
  /* USER CODE BEGIN OnMacProcessNotify_1 */

  /* USER CODE END OnMacProcessNotify_1 */
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_LmHandlerProcess), CFG_SEQ_Prio_0);

  /* USER CODE BEGIN OnMacProcessNotify_2 */

  /* USER CODE END OnMacProcessNotify_2 */
}

static void OnTxPeriodicityChanged(uint32_t periodicity)
{
  /* USER CODE BEGIN OnTxPeriodicityChanged_1 */

  /* USER CODE END OnTxPeriodicityChanged_1 */
  TxPeriodicity = periodicity;

  if (TxPeriodicity == 0)
  {
    /* Revert to application default periodicity */
    TxPeriodicity = APP_TX_DUTYCYCLE;
  }

  /* Update timer periodicity */
  //UTIL_TIMER_Stop(&TxTimer);
  //UTIL_TIMER_SetPeriod(&TxTimer, TxPeriodicity);
  //UTIL_TIMER_Start(&TxTimer);
  /* USER CODE BEGIN OnTxPeriodicityChanged_2 */

  /* USER CODE END OnTxPeriodicityChanged_2 */
}

static void OnTxFrameCtrlChanged(LmHandlerMsgTypes_t isTxConfirmed)
{
  /* USER CODE BEGIN OnTxFrameCtrlChanged_1 */

  /* USER CODE END OnTxFrameCtrlChanged_1 */
  LmHandlerParams.IsTxConfirmed = isTxConfirmed;
  /* USER CODE BEGIN OnTxFrameCtrlChanged_2 */

  /* USER CODE END OnTxFrameCtrlChanged_2 */
}

static void OnPingSlotPeriodicityChanged(uint8_t pingSlotPeriodicity)
{
  /* USER CODE BEGIN OnPingSlotPeriodicityChanged_1 */

  /* USER CODE END OnPingSlotPeriodicityChanged_1 */
  LmHandlerParams.PingSlotPeriodicity = pingSlotPeriodicity;
  /* USER CODE BEGIN OnPingSlotPeriodicityChanged_2 */

  /* USER CODE END OnPingSlotPeriodicityChanged_2 */
}

static void OnSystemReset(void)
{
  /* USER CODE BEGIN OnSystemReset_1 */

  /* USER CODE END OnSystemReset_1 */
  if ((LORAMAC_HANDLER_SUCCESS == LmHandlerHalt()) && (LmHandlerJoinStatus() == LORAMAC_HANDLER_SET))
  {
    NVIC_SystemReset();
  }
  /* USER CODE BEGIN OnSystemReset_Last */

  /* USER CODE END OnSystemReset_Last */
}



static void StoreContext(void)
{
  LmHandlerErrorStatus_t status = LORAMAC_HANDLER_ERROR;

  /* USER CODE BEGIN StoreContext_1 */

  /* USER CODE END StoreContext_1 */
  status = LmHandlerNvmDataStore();

  if (status == LORAMAC_HANDLER_NVM_DATA_UP_TO_DATE)
  {
    APP_LOG(TS_OFF, VLEVEL_M, "NVM DATA UP TO DATE\r\n");
  }
  else if (status == LORAMAC_HANDLER_ERROR)
  {
    APP_LOG(TS_OFF, VLEVEL_M, "NVM DATA STORE FAILED\r\n");
  }
  /* USER CODE BEGIN StoreContext_Last */

  /* USER CODE END StoreContext_Last */
}

static void OnNvmDataChange(LmHandlerNvmContextStates_t state)
{
  /* USER CODE BEGIN OnNvmDataChange_1 */

  /* USER CODE END OnNvmDataChange_1 */
  if (state == LORAMAC_HANDLER_NVM_STORE)
  {
    APP_LOG(TS_OFF, VLEVEL_M, "NVM DATA STORED\r\n");
  }
  else
  {
    APP_LOG(TS_OFF, VLEVEL_M, "NVM DATA RESTORED\r\n");
  }
  /* USER CODE BEGIN OnNvmDataChange_Last */

  /* USER CODE END OnNvmDataChange_Last */
}

static void OnStoreContextRequest(void *nvm, uint32_t nvm_size)
{
  /* USER CODE BEGIN OnStoreContextRequest_1 */

  /* USER CODE END OnStoreContextRequest_1 */
  /* store nvm in flash */
  if (FLASH_IF_Erase(LORAWAN_NVM_BASE_ADDRESS, FLASH_PAGE_SIZE) == FLASH_IF_OK)
  {
    FLASH_IF_Write(LORAWAN_NVM_BASE_ADDRESS, (const void *)nvm, nvm_size);
  }
  /* USER CODE BEGIN OnStoreContextRequest_Last */

  /* USER CODE END OnStoreContextRequest_Last */
}

static void OnRestoreContextRequest(void *nvm, uint32_t nvm_size)
{
  /* USER CODE BEGIN OnRestoreContextRequest_1 */

  /* USER CODE END OnRestoreContextRequest_1 */
  FLASH_IF_Read(nvm, LORAWAN_NVM_BASE_ADDRESS, nvm_size);
  /* USER CODE BEGIN OnRestoreContextRequest_Last */

  /* USER CODE END OnRestoreContextRequest_Last */
}

