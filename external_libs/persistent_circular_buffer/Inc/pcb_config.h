#ifndef PCB_CONFIG_H
#define PCB_CONFIG_H

#include <stdint.h>

/*
 * STM32WLE5JCIx main Flash:
 *   0x08000000 - 0x0803FFFF, 256 KiB
 *   Page size: 2 KiB
 *
 * This first version reserves the final two pages:
 *   Page 126: 0x0803F000 - 0x0803F7FF
 *   Page 127: 0x0803F800 - 0x0803FFFF
 *
 * IMPORTANT: Remove these 4 KiB from the application FLASH region in the
 * linker script. Merely defining these addresses in C does not reserve them.
 */
#define PCB_FLASH_PAGE_SIZE_BYTES       (2048UL)

#define PCB_FLASH_PAGE_A_NUMBER         (124UL)
#define PCB_FLASH_PAGE_A_ADDRESS        (0x0803E000UL)

#define PCB_FLASH_PAGE_B_NUMBER         (125UL)
#define PCB_FLASH_PAGE_B_ADDRESS        (0x0803E800UL)

#define PCB_CAPACITY                    (100U)

#define PCB_SNAPSHOT_MAGIC              (0x50434231UL) /* "PCB1" */
#define PCB_RECORD_FORMAT_VERSION       (1U)

/* Written only after the header and payload have been verified. */
#define PCB_COMMIT_MARKER               (0x504342434F4D4954ULL)

/* Read back each programmed block during pcb_sync(). */
#define PCB_ENABLE_FULL_READBACK_VERIFY (1U)

#endif /* PCB_CONFIG_H */
