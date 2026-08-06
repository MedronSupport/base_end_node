#include "MFRC522_STM32.h"


uint8_t atqa[2];

SPI_HandleTypeDef hspi2;
void MFRC522_Gpio_Init(void) {

	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	HAL_GPIO_WritePin(GPIOB, CS_Pin | SPI_RESET_Pin, GPIO_PIN_RESET);

	GPIO_InitStruct.Pin = CS_Pin | SPI_RESET_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	//MFRC VCC Power control pin
	HAL_GPIO_WritePin(MFRC_PWR_GPIO_Port,MFRC_PWR_Pin, GPIO_PIN_RESET);

	GPIO_InitStruct.Pin = MFRC_PWR_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(MFRC_PWR_GPIO_Port, &GPIO_InitStruct);

}
void MFRC522_Spi_Init(void)
{

  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }

}
void MFRC522_Power_Reset_By_GPIO(void)
{
	HAL_GPIO_WritePin(MFRC_PWR_GPIO_Port, MFRC_PWR_Pin, GPIO_PIN_RESET);
}
void MFRC522_Power_On_By_GPIO(void)
{
	HAL_GPIO_WritePin(MFRC_PWR_GPIO_Port, MFRC_PWR_Pin, GPIO_PIN_SET);
}
void MFRC522_Hardware_Reset_By_GPIO(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
	HAL_GPIO_WritePin(GPIOx, GPIO_Pin, GPIO_PIN_RESET);
}
void MFRC522_Spi_Deinit(void)
{
	  if (HAL_SPI_DeInit(&hspi2) != HAL_OK)
	  {
	    Error_Handler();
	  }
}
void MFRC522_Init(MFRC522_t *dev)
{
    USER_LOG("MFRC522 Min Init started");
    // Hardware reset
    HAL_GPIO_WritePin(dev->rstPort, dev->rstPin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(dev->rstPort, dev->rstPin, GPIO_PIN_SET);
    HAL_Delay(50);

    // Soft reset
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_SoftReset);
    HAL_Delay(50);

    // Clear interrupts
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);

    // Flush FIFO
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);

    // Timer: ~25ms timeout
    MFRC522_WriteReg(dev, PCD_TModeReg, 0x80);      // Timer starts immediately
    MFRC522_WriteReg(dev, PCD_TPrescalerReg, 0xA9); // 80kHz clock
    MFRC522_WriteReg(dev, PCD_TReloadRegH, 0x03);   // 1000 ticks = ~12.5ms
    MFRC522_WriteReg(dev, PCD_TReloadRegL, 0xE8);

    // RF settings
    MFRC522_WriteReg(dev, PCD_TxAutoReg, 0x40);     // 100% ASK modulation
    MFRC522_WriteReg(dev, PCD_RFCfgReg, 0x7F);      // Max gain (48dB)
    MFRC522_WriteReg(dev, PCD_DemodReg, 0x4D);      // Sensitivity for clones

    // Antenna driver strength (lower = less current, shorter read range)
    // Factory defaults: GsNReg=0x88, CWGsPReg=0x3F, ModGsPReg=0x10
    MFRC522_WriteReg(dev, PCD_GsNReg,    0x40);
    MFRC522_WriteReg(dev, PCD_CWGsPReg,  0x20);
    MFRC522_WriteReg(dev, PCD_ModGsPReg, 0x08);

    // Enable antenna
    MFRC522_AntennaOn(dev);
    HAL_Delay(10);  // Let RF stabilize

    uint8_t version = MFRC522_ReadReg(dev, PCD_VersionReg);
    if ((version != 0x91)||(version != 0x92)){
    	USER_LOG("Version: 0x%02X (counterfeit OK for UID)", version);
    }
    else USER_LOG("Version: 0x%02X", version);
    uint8_t txCtrl = MFRC522_ReadReg(dev, PCD_TxControlReg);
    DEBUG_LOG("TxControlReg: 0x%02X (expect >= 0x03)", txCtrl);
    USER_LOG("MFRC522 Min Init complete");
}
void MFRC522_Hardware_Reset(MFRC522_t *dev)
{
    USER_LOG("MFRC522 Hardware Reset triggered...");
    // Hardware reset
    HAL_GPIO_WritePin(dev->rstPort, dev->rstPin, GPIO_PIN_RESET);
    HAL_Delay(50);
}
void MFRC522_AntennaOff(MFRC522_t *dev) {
    MFRC522_ClearBitMask(dev, PCD_TxControlReg, 0x03);
   // DEBUG_LOG("Antenna off");
}

void MFRC522_AntennaOn(MFRC522_t *dev) {
    MFRC522_SetBitMask(dev, PCD_TxControlReg, 0x03);
    DEBUG_LOG("Antenna on");
}

uint8_t MFRC522_ReadReg(MFRC522_t *dev, uint8_t reg) {
    uint8_t addr = ((reg << 1) & 0x7E) | 0x80;
    uint8_t val = 0;
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(dev->hspi, &addr, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(dev->hspi, &val, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_SET);
    HAL_Delay(1);
   // DEBUG_LOG("ReadReg: 0x%02X -> 0x%02X", reg, val);
    return val;
}

void MFRC522_WriteReg(MFRC522_t *dev, uint8_t reg, uint8_t value) {
    uint8_t addr = (reg << 1) & 0x7E;
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(dev->hspi, &addr, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(dev->hspi, &value, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_SET);
    HAL_Delay(1);
   // DEBUG_LOG("WriteReg: 0x%02X = 0x%02X", reg, value);
}

void MFRC522_SetBitMask(MFRC522_t *dev, uint8_t reg, uint8_t mask) {
    uint8_t tmp = MFRC522_ReadReg(dev, reg);
    MFRC522_WriteReg(dev, reg, tmp | mask);
   // DEBUG_LOG("SetBitMask: 0x%02X |= 0x%02X", reg, mask);
}

void MFRC522_ClearBitMask(MFRC522_t *dev, uint8_t reg, uint8_t mask) {
    uint8_t tmp = MFRC522_ReadReg(dev, reg);
    MFRC522_WriteReg(dev, reg, tmp & (~mask));
  ///  DEBUG_LOG("ClearBitMask: 0x%02X &= ~0x%02X", reg, mask);
}

uint8_t MFRC522_RequestA(MFRC522_t *dev, uint8_t *atqa) {
   // DEBUG_LOG("RequestA");
    MFRC522_AntennaOff(dev);  // Reset RF
    HAL_Delay(5);  // Allow chip to stabilize
    MFRC522_AntennaOn(dev);
    HAL_Delay(5);  // Ensure RF is ready
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
    MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x07);  // 7 bits for REQA
    MFRC522_WriteReg(dev, PCD_FIFODataReg, PICC_REQA);
    HAL_Delay(2);  // Increased for counterfeit chip stability
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

    // Poll for completion (25ms timeout)
    uint32_t timeout = HAL_GetTick() + 25;
    while (HAL_GetTick() < timeout) {
        uint8_t status2 = MFRC522_ReadReg(dev, PCD_Status2Reg);
        if (status2 & 0x01) {  // Command complete
            uint8_t err = MFRC522_ReadReg(dev, PCD_ErrorReg);
            if (err & 0x1D) {  // Protocol/parity/buffer errors
                DEBUG_LOG("RequestA error: 0x%02X", err);
                MFRC522_AntennaOff(dev);
                HAL_Delay(5);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle); // Stop command
                return STATUS_ERROR;
            }
            uint8_t fifoLvl = MFRC522_ReadReg(dev, PCD_FIFOLevelReg);
            if (fifoLvl >= 2) {  // ATQA is 2 bytes
                atqa[0] = MFRC522_ReadReg(dev, PCD_FIFODataReg);
                atqa[1] = MFRC522_ReadReg(dev, PCD_FIFODataReg);
                DEBUG_LOG("RequestA ATQA: 0x%02X 0x%02X", atqa[0], atqa[1]);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle); // Stop command
                HAL_Delay(2);  // Post-command delay
                return STATUS_OK;
            }
            DEBUG_LOG("RequestA bad FIFO level: %d", fifoLvl);
            MFRC522_AntennaOff(dev);
            HAL_Delay(5);
            MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
            return STATUS_ERROR;
        }
        HAL_Delay(1);  // Mimic debug log timing
    }
    DEBUG_LOG("RequestA timeout");
    MFRC522_AntennaOff(dev);
    HAL_Delay(5);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
    return STATUS_TIMEOUT;
}
uint8_t MFRC522_Select(MFRC522_t *dev, uint8_t *uid_and_bcc, uint8_t sel_level) {
    uint8_t buffer[7];
    uint8_t i;

    // 1. İletişim ve FIFO ayarları
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Tüm IRQ bayraklarını temizle[cite: 1]
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // FIFO'yu temizle[cite: 1]

    // 2. Otomatik CRC üretimini aktif et (Select komutu ISO standardı gereği CRC içerir)
    MFRC522_SetBitMask(dev, PCD_TxModeReg, 0x80);    // TxCRCEn[cite: 1]
    MFRC522_SetBitMask(dev, PCD_RxModeReg, 0x80);    // RxCRCEn

    // 3. Paket içeriğini hazirla: [Cascade/Select Kod] + [NVB (0x70 = 7 bayt)] + [UID(4 bayt)] + [BCC]
    buffer[0] = sel_level; // PICC_SEL_CL1 (0x93) veya PICC_SEL_CL2 (0x95)
    buffer[1] = 0x70;      // NVB: Tam blok seçimi (7 bayt geçerli)
    for (i = 0; i < 5; i++) {
        buffer[2 + i] = uid_and_bcc[i];
    }

    // 4. Verileri FIFO belleğine yaz
    for (i = 0; i < 7; i++) {
        MFRC522_WriteReg(dev, PCD_FIFODataReg, buffer[i]);
    }

    // 5. Transceive komutunu başlat ve iletimi tetikle
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);       // StartSend[cite: 1]

    // 6. Karttan SAK (Select Acknowledgment) yanıtını bekle (25ms timeout)
    uint32_t timeout = HAL_GetTick() + 25;
    while (HAL_GetTick() < timeout) {
        uint8_t status2 = MFRC522_ReadReg(dev, PCD_Status2Reg);
        if (status2 & 0x01) { // İşlem tamamlandı
            uint8_t err = MFRC522_ReadReg(dev, PCD_ErrorReg);
            if (err & 0x1D) {
                // ÇIKIŞTA CRC'LERİ KAPAT
                MFRC522_ClearBitMask(dev, PCD_TxModeReg, 0x80);
                MFRC522_ClearBitMask(dev, PCD_RxModeReg, 0x80);
                MFRC522_AntennaOff(dev);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                return STATUS_ERROR;
            }

            uint8_t fifoLvl = MFRC522_ReadReg(dev, PCD_FIFOLevelReg);
            if (fifoLvl >= 1) {
                (void)MFRC522_ReadReg(dev, PCD_FIFODataReg);

                // ÇIKIŞTA CRC'LERİ KAPAT (REQA ve Anticollision CRC kullanmaz!)
                MFRC522_ClearBitMask(dev, PCD_TxModeReg, 0x80);
                MFRC522_ClearBitMask(dev, PCD_RxModeReg, 0x80);

                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                return STATUS_OK;
            }
        }
        HAL_Delay(1);
    }

    // TIMEOUT ÇIKIŞINDA DA CRC'LERİ KAPAT
    MFRC522_ClearBitMask(dev, PCD_TxModeReg, 0x80);
    MFRC522_ClearBitMask(dev, PCD_RxModeReg, 0x80);
    MFRC522_AntennaOff(dev);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
    return STATUS_TIMEOUT;
}
uint8_t MFRC522_Anticoll(MFRC522_t *dev, uint8_t *uid,uint8_t* len) {  // Returns 4-byte/7-byte UID + BCC

	uint8_t uidtemp[5]={0};
	uint8_t uidtemp2[5]={0};
    DEBUG_LOG("Anticoll");
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
    MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x00);  // Full frame
    MFRC522_WriteReg(dev, PCD_FIFODataReg, PICC_SEL_CL1);  // 0x93
    MFRC522_WriteReg(dev, PCD_FIFODataReg, 0x20);    // first two bytes
    HAL_Delay(2);  // Delay for stability
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

    uint32_t timeout = HAL_GetTick() + 25;
    while (HAL_GetTick() < timeout) {
        uint8_t status2 = MFRC522_ReadReg(dev, PCD_Status2Reg);
        if (status2 & 0x01) {  // Command complete
            uint8_t err = MFRC522_ReadReg(dev, PCD_ErrorReg);
            if (err & 0x1D) {
                DEBUG_LOG("Anticoll error: 0x%02X", err);
                MFRC522_AntennaOff(dev);
                HAL_Delay(5);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                return STATUS_ERROR;
            }
            uint8_t fifoLvl = MFRC522_ReadReg(dev, PCD_FIFOLevelReg);
            if (fifoLvl == 5) {  // 4-byte UID + BCC
                for (int i = 0; i < 5; i++) {
                	uidtemp[i] = MFRC522_ReadReg(dev, PCD_FIFODataReg);
                }

                // Validate BCC
                uint8_t calcBcc = uidtemp[0] ^ uidtemp[1] ^ uidtemp[2] ^ uidtemp[3];
                if (uidtemp[4] != calcBcc) {
                    DEBUG_LOG("Anticoll bad BCC: calc=0x%02X, got=0x%02X", calcBcc, uidtemp[4]);
                    MFRC522_AntennaOff(dev);
                    HAL_Delay(5);
                    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                    return STATUS_ERROR;
                }
				if (uidtemp[0] == 0x88) {
					//7 byte veri
					MFRC522_Select(dev, uidtemp, PICC_SEL_CL1);
					*len = 7;
					MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);    // Clear IRQs
					MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80); // Flush FIFO
					MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x00); // Full frame
					MFRC522_WriteReg(dev, PCD_FIFODataReg, PICC_SEL_CL2); // 0x95
					MFRC522_WriteReg(dev, PCD_FIFODataReg, 0x20); // first two bytes
					HAL_Delay(2);  // Delay for stability
					MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
					MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);
					uint32_t timeout2 = HAL_GetTick() + 25;
					while (HAL_GetTick() < timeout2) {
						uint8_t status2_2 = MFRC522_ReadReg(dev,PCD_Status2Reg);
						if (status2_2 & 0x01) {  // Command complete
							uint8_t err2 = MFRC522_ReadReg(dev, PCD_ErrorReg);
							if (err2 & 0x1D) {
								DEBUG_LOG("Anticoll error: 0x%02X", err2);
								MFRC522_AntennaOff(dev);
								HAL_Delay(5);
								MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
								return STATUS_ERROR;
							}
							uint8_t fifoLvl2 = MFRC522_ReadReg(dev,PCD_FIFOLevelReg);
							if (fifoLvl2 == 5) {  // 4-byte UID + BCC
								for (int i = 0; i < 5; i++) {
									uidtemp2[i] = MFRC522_ReadReg(dev,PCD_FIFODataReg);
								}
								MFRC522_Select(dev, uidtemp2, PICC_SEL_CL2);
								// Validate BCC
								uint8_t calcBcc = uidtemp2[0] ^ uidtemp2[1] ^ uidtemp2[2]^ uidtemp2[3];
								if (uidtemp2[4] != calcBcc) {
									DEBUG_LOG("Anticoll bad BCC: calc=0x%02X, got=0x%02X", calcBcc, uidtemp2[4]);
									MFRC522_AntennaOff(dev);
									HAL_Delay(5);
									MFRC522_WriteReg(dev, PCD_CommandReg,PCD_Idle);
									return STATUS_ERROR;
								}
								uid[0]=uidtemp[1];
								uid[1]=uidtemp[2];
								uid[2]=uidtemp[3];
								uid[3]=uidtemp2[0];
								uid[4]=uidtemp2[1];
								uid[5]=uidtemp2[2];
								uid[6]=uidtemp2[3];
				                DEBUG_LOG("Anticoll UID: %02X %02X %02X %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
				                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
				                HAL_Delay(2);  // Post-command delay
				                return STATUS_OK;

							}

						}
						HAL_Delay(1);  // Mimic debug log timing
					}
				    DEBUG_LOG("Anticoll timeout");
				    MFRC522_AntennaOff(dev);
				    HAL_Delay(5);
				    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
				    return STATUS_TIMEOUT;
				} else {
					//4 byte veri
					*len = 4;
					uid[0] = uidtemp[0];
					uid[1] = uidtemp[1];
					uid[2] = uidtemp[2];
					uid[3] = uidtemp[3];
				}
                DEBUG_LOG("Anticoll UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                HAL_Delay(2);  // Post-command delay
                return STATUS_OK;
            }

            DEBUG_LOG("Anticoll bad FIFO level: %d", fifoLvl);
            MFRC522_AntennaOff(dev);
            HAL_Delay(5);
            MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
            return STATUS_ERROR;
        }
        HAL_Delay(1);  // Mimic debug log timing
    }
    DEBUG_LOG("Anticoll timeout");
    MFRC522_AntennaOff(dev);
    HAL_Delay(5);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
    return STATUS_TIMEOUT;
}

uint8_t MFRC522_ReadUid(MFRC522_t *dev, volatile uint8_t *uid,uint8_t* len)
{
    DEBUG_LOG("Reading UID...");
    // Card detected, read UID
    uint8_t rawUid[MAXIMUM_LEN_UUID]={0};
    if (MFRC522_Anticoll(dev, rawUid,len) != STATUS_OK) {
    	DEBUG_LOG("Anticollision failed");
        return STATUS_ERROR;
    }
    // Copy UID (drop BCC)
    for (int i = 0; i < *len; i++) {
        uid[i] = rawUid[i];
    }
    //DEBUG_LOG("Card UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
    return STATUS_OK;
}

uint8_t waitcardRemoval (MFRC522_t *dev){
    USER_LOG("Waiting for card removal...");
    while (1) {
        if (MFRC522_RequestA(dev, atqa) != STATUS_OK) {
        	USER_LOG("Card removed");
            return STATUS_OK; // Card removed, return success
        }
        HAL_Delay(100); // Poll every 100ms to check if card is still present
    }
}
uint8_t waitcardRemovalUntilTimeout(MFRC522_t *dev, uint32_t timeout_inms) {
	uint32_t start_tick = HAL_GetTick();
	uint32_t current_tick = start_tick;
	uint32_t calc_ms_from_tick = 0;
	while (calc_ms_from_tick < timeout_inms) {

		if (MFRC522_RequestA(dev, atqa) != STATUS_OK) {
			USER_LOG("Card removed");
			return STATUS_OK; // Card removed, return success
		}
		HAL_Delay(100); // Poll every 100ms to check if card is still present
		current_tick = HAL_GetTick();
		calc_ms_from_tick = (current_tick - start_tick)
				* ((uint32_t) HAL_GetTickFreq());
	}
	return STATUS_TIMEOUT;
}
uint8_t waitcardDetect (MFRC522_t *dev){
	atqa[0] = atqa[1] = 0;
	USER_LOG("Waiting for the card...");
	while (1){
	    if (MFRC522_RequestA(dev, atqa) == STATUS_OK) {
	    	USER_LOG("Card detected");
	        return STATUS_OK;
	    }
	    HAL_Delay(100);	// Poll every 100ms to check if card is  present
	}
}
uint8_t waitcardDetectUntilTimeout(MFRC522_t *dev, uint32_t timeout_ms) {
	uint32_t start_tick = HAL_GetTick();
	uint32_t current_tick = start_tick;
	uint32_t calc_ms_from_tick = 0;
	while (calc_ms_from_tick < timeout_ms) {
        if (MFRC522_RequestA(dev, atqa) == STATUS_OK) {
            return STATUS_OK;
        }
        HAL_Delay(100);
		current_tick = HAL_GetTick();
		calc_ms_from_tick = (current_tick - start_tick)
				* ((uint32_t) HAL_GetTickFreq());
    }
    return STATUS_TIMEOUT;
}
void rfid_read_process_init(MFRC522_t *dev)
{

	MFRC522_Spi_Init();
	MFRC522_Init(dev);
}


