#ifndef PCB_FLASH_PORT_H
#define PCB_FLASH_PORT_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    PCB_FLASH_OK = 0,
    PCB_FLASH_INVALID_ARGUMENT,
    PCB_FLASH_UNLOCK_ERROR,
    PCB_FLASH_ERASE_ERROR,
    PCB_FLASH_PROGRAM_ERROR,
    PCB_FLASH_LOCK_ERROR,
    PCB_FLASH_VERIFY_ERROR
} pcb_flash_result_t;

pcb_flash_result_t pcb_flash_unlock(void);
pcb_flash_result_t pcb_flash_lock(void);

pcb_flash_result_t pcb_flash_erase_page(uint32_t page_number);

/* Address and length must be multiples of the STM32WL 8-byte program unit. */
pcb_flash_result_t pcb_flash_program(
    uint32_t address,
    const void *data,
    size_t length);

pcb_flash_result_t pcb_flash_read(
    uint32_t address,
    void *destination,
    size_t length);

pcb_flash_result_t pcb_flash_verify(
    uint32_t address,
    const void *expected,
    size_t length);

uint32_t pcb_flash_last_hal_error(void);

#endif /* PCB_FLASH_PORT_H */
