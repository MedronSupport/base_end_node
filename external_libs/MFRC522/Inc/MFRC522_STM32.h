#ifndef MFRC522_STM32_MIN_H
#define MFRC522_STM32_MIN_H

#include "stm32wlxx_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "main.h"
//RFID READ TIOMEOUT
#define MFRC_RFID_READ_TIMEOUT 5000
#define MFRC_RFID_WAIT_DETECT_TIMEOUT 300
#define MFRC_RFID_WAIT_REMOVE_TIMEOUT 300
//GPIO
#define SPI_RESET_Pin GPIO_PIN_10
#define SPI_RESET_GPIO_Port GPIOB
#define CS_Pin GPIO_PIN_9
#define CS_GPIO_Port GPIOB
#define MFRC_PWR_Pin GPIO_PIN_9
#define MFRC_PWR_GPIO_Port GPIOA

#define ENABLE_USER_LOG   1
#define ENABLE_DEBUG_LOG  0 // Test with this disabled

#define MAXIMUM_LEN_UUID 7

typedef struct{
	uint8_t uid[MAXIMUM_LEN_UUID];
	bool is_uuid_data_assigned;
	uint8_t uuid_len;
}uuid_t;

#if ENABLE_USER_LOG
  #define USER_LOG(fmt, ...) printf("[USER] " fmt "\r\n", ##__VA_ARGS__)
#else
  #define USER_LOG(fmt, ...)
#endif

#if ENABLE_DEBUG_LOG
  #define DEBUG_LOG(fmt, ...) printf("[DEBUG] " fmt "\r\n", ##__VA_ARGS__)
#else
  #define DEBUG_LOG(fmt, ...)
#endif

// Essential registers
#define PCD_CommandReg     0x01
#define PCD_ComIrqReg      0x04
#define PCD_ErrorReg       0x06
#define PCD_Status2Reg     0x08
#define PCD_FIFODataReg    0x09
#define PCD_FIFOLevelReg   0x0A
#define PCD_BitFramingReg  0x0D
#define PCD_TxModeReg	   0x12
#define PCD_RxModeReg	   0x13
#define PCD_TxControlReg   0x14
#define PCD_TxAutoReg      0x15
#define PCD_RFCfgReg       0x26
#define PCD_GsNReg         0x27
#define PCD_CWGsPReg       0x28
#define PCD_ModGsPReg      0x29
#define PCD_TModeReg       0x2A
#define PCD_TPrescalerReg  0x2B
#define PCD_TReloadRegL    0x2C
#define PCD_TReloadRegH    0x2D
#define PCD_DemodReg       0x19
#define PCD_VersionReg     0x37

// Commands
#define PCD_Idle           0x00
#define PCD_Transceive     0x0C
#define PCD_SoftReset      0x0F

// PICC commands
#define PICC_REQA          0x26
#define PICC_SEL_CL1       0x93
#define PICC_SEL_CL2       0x95

// Status
#define STATUS_OK          0
#define STATUS_ERROR       1
#define STATUS_TIMEOUT     2

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *csPort;
    uint16_t csPin;
    GPIO_TypeDef *rstPort;
    uint16_t rstPin;
} MFRC522_t;


// Prototypes
void MFRC522_Init(MFRC522_t *dev);
void MFRC522_AntennaOff(MFRC522_t *dev);
void MFRC522_AntennaOn(MFRC522_t *dev);
uint8_t MFRC522_ReadReg(MFRC522_t *dev, uint8_t reg);
void MFRC522_WriteReg(MFRC522_t *dev, uint8_t reg, uint8_t value);
void MFRC522_SetBitMask(MFRC522_t *dev, uint8_t reg, uint8_t mask);
void MFRC522_ClearBitMask(MFRC522_t *dev, uint8_t reg, uint8_t mask);
uint8_t MFRC522_RequestA(MFRC522_t *dev, uint8_t *atqa);
uint8_t MFRC522_Anticoll(MFRC522_t *dev, uint8_t *uid,uint8_t* len);
uint8_t MFRC522_ReadUid(MFRC522_t *dev, volatile uint8_t *uid,uint8_t* len);
uint8_t waitcardRemoval (MFRC522_t *dev);
uint8_t waitcardRemovalUntilTimeout(MFRC522_t *dev, uint32_t timeout_inms);
uint8_t waitcardDetect (MFRC522_t *dev);
uint8_t waitcardDetectUntilTimeout(MFRC522_t *dev, uint32_t timeout_ms);
void rfid_read_process_init(MFRC522_t *dev);
void MFRC522_Gpio_Init(void);
void MFRC522_Spi_Init(void);
void MFRC522_Spi_Deinit(void);
void MFRC522_Hardware_Reset(MFRC522_t *dev) ;
void MFRC522_Hardware_Reset_By_GPIO(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void MFRC522_Power_Reset_By_GPIO(void);
void MFRC522_Power_On_By_GPIO(void);
#endif
