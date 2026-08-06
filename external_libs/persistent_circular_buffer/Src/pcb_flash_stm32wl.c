#include "pcb_flash_port.h"

#include "stm32wlxx_hal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint32_t s_last_hal_error = 0U;

static bool pcb_is_aligned_8(uint32_t value)
{
    return (value & 0x7UL) == 0UL;
}

pcb_flash_result_t pcb_flash_unlock(void)
{
    s_last_hal_error = 0U;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return PCB_FLASH_UNLOCK_ERROR;
    }

#if defined(FLASH_FLAG_ALL_ERRORS)
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
#endif

    return PCB_FLASH_OK;
}

pcb_flash_result_t pcb_flash_lock(void)
{
    if (HAL_FLASH_Lock() != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return PCB_FLASH_LOCK_ERROR;
    }

    return PCB_FLASH_OK;
}

pcb_flash_result_t pcb_flash_erase_page(uint32_t page_number)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0xFFFFFFFFUL;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Page      = page_number;
    erase.NbPages   = 1UL;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return PCB_FLASH_ERASE_ERROR;
    }

    if (page_error != 0xFFFFFFFFUL)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return PCB_FLASH_ERASE_ERROR;
    }

    return PCB_FLASH_OK;
}

pcb_flash_result_t pcb_flash_program(
    uint32_t address,
    const void *data,
    size_t length)
{
    const uint8_t *source = (const uint8_t *)data;

    if ((data == NULL) ||
        (length == 0U) ||
        ((length & 0x7U) != 0U) ||
        !pcb_is_aligned_8(address))
    {
        return PCB_FLASH_INVALID_ARGUMENT;
    }

    for (size_t offset = 0U; offset < length; offset += 8U)
    {
        uint64_t double_word = UINT64_MAX;

        memcpy(&double_word, source + offset, sizeof(double_word));

        /*
         * Do not program an all-ones double-word. The visible value would
         * remain erased, but ECC bits can still be programmed. Skipping it
         * also guarantees that the commit-marker location stays pristine
         * until the final commit operation.
         */
        if (double_word == UINT64_MAX)
        {
            continue;
        }

        if (HAL_FLASH_Program(
                FLASH_TYPEPROGRAM_DOUBLEWORD,
                address + (uint32_t)offset,
                double_word) != HAL_OK)
        {
            s_last_hal_error = HAL_FLASH_GetError();
            return PCB_FLASH_PROGRAM_ERROR;
        }
    }

    return PCB_FLASH_OK;
}

pcb_flash_result_t pcb_flash_read(
    uint32_t address,
    void *destination,
    size_t length)
{
    if ((destination == NULL) || (length == 0U))
    {
        return PCB_FLASH_INVALID_ARGUMENT;
    }

    memcpy(destination, (const void *)(uintptr_t)address, length);
    return PCB_FLASH_OK;
}

pcb_flash_result_t pcb_flash_verify(
    uint32_t address,
    const void *expected,
    size_t length)
{
    if ((expected == NULL) || (length == 0U))
    {
        return PCB_FLASH_INVALID_ARGUMENT;
    }

    if (memcmp((const void *)(uintptr_t)address, expected, length) != 0)
    {
        return PCB_FLASH_VERIFY_ERROR;
    }

    return PCB_FLASH_OK;
}

uint32_t pcb_flash_last_hal_error(void)
{
    return s_last_hal_error;
}
