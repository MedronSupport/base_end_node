#ifndef PERSISTENT_CIRCULAR_BUFFER_H
#define PERSISTENT_CIRCULAR_BUFFER_H

#include "pcb_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PCB_STATUS_FAILED = 0,
    PCB_STATUS_SENT   = 1
} pcb_record_status_t;

/*
 * Fixed 16-byte RAM/Flash record.
 *
 * uuid_length == 4: uuid[0..3] is valid; uuid[4..6] is cleared.
 * uuid_length == 7: uuid[0..6] is valid.
 *
 * record_id is allocated by the library and remains a stable handle while
 * the record is still present in the 100-entry circular buffer.
 */
typedef struct
{
    uint32_t timestamp;
    uint8_t  uuid[7];
    uint8_t  uuid_length;
    uint16_t record_id;
    uint8_t  status;
    uint8_t  reserved;
} pcb_record_t;

typedef enum
{
    PCB_OK = 0,
    PCB_TRUNCATED,
    PCB_ERROR_INVALID_ARGUMENT,
    PCB_ERROR_NOT_INITIALIZED,
    PCB_ERROR_ALREADY_INITIALIZED,
    PCB_ERROR_EMPTY,
    PCB_ERROR_NOT_FOUND,
    PCB_ERROR_BUSY,
    PCB_ERROR_FLASH_UNLOCK,
    PCB_ERROR_FLASH_ERASE,
    PCB_ERROR_FLASH_PROGRAM,
    PCB_ERROR_FLASH_LOCK,
    PCB_ERROR_FLASH_VERIFY,
    PCB_ERROR_SNAPSHOT_CORRUPT
} pcb_result_t;

/*
 * Declare the handle as static/global or initialize it with {0} before the
 * first pcb_init() call.
 */
typedef struct
{
    pcb_record_t records[PCB_CAPACITY];

    uint16_t write_index;
    uint16_t count;
    uint16_t next_record_id;

    uint32_t generation;

    uint8_t active_page;
    bool initialized;
    bool dirty;
    bool busy;
} pcb_handle_t;

#define PCB_HANDLE_INITIALIZER {0}

/* Load the newest valid A/B Flash snapshot into the RAM circular buffer. */
pcb_result_t pcb_init(pcb_handle_t *handle);

/*
 * If dirty, save the RAM snapshot. The handle is cleared only after a
 * successful save. RAM remains intact if Flash persistence fails.
 */
pcb_result_t pcb_deinit(pcb_handle_t *handle);

/* Save the current RAM state to the inactive Flash page. */
pcb_result_t pcb_sync(pcb_handle_t *handle);

/* Add to RAM only. The oldest record is overwritten when the buffer is full. */
pcb_result_t pcb_add(
    pcb_handle_t *handle,
    uint32_t timestamp,
    const uint8_t *uuid,
    uint8_t uuid_length,
    pcb_record_status_t status,
    uint16_t *out_record_id);

pcb_result_t pcb_set_status_by_id(
    pcb_handle_t *handle,
    uint16_t record_id,
    pcb_record_status_t new_status);

/* Update the newest record matching timestamp + UUID. */
pcb_result_t pcb_set_status_by_key(
    pcb_handle_t *handle,
    uint32_t timestamp,
    const uint8_t *uuid,
    uint8_t uuid_length,
    pcb_record_status_t new_status);

pcb_result_t pcb_get_latest(
    const pcb_handle_t *handle,
    pcb_record_t *out_record);

/* Newest record matching the selected status (PCB_ERROR_NOT_FOUND if none). */
pcb_result_t pcb_get_latest_by_status(
    const pcb_handle_t *handle,
    pcb_record_status_t status,
    pcb_record_t *out_record);

/* Results are returned oldest-to-newest within the selected result set. */
pcb_result_t pcb_get_last_n(
    const pcb_handle_t *handle,
    size_t requested_count,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count);

/*
 * Paged recent query:
 *   skip_from_latest=0, count=10  -> newest ten records
 *   skip_from_latest=10, count=10 -> preceding ten records
 */
pcb_result_t pcb_get_recent_slice(
    const pcb_handle_t *handle,
    size_t skip_from_latest,
    size_t requested_count,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count);

/* Inclusive timestamp query. */
pcb_result_t pcb_get_by_timestamp(
    const pcb_handle_t *handle,
    uint32_t start_timestamp,
    uint32_t end_timestamp,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count);

pcb_result_t pcb_get_by_status(
    const pcb_handle_t *handle,
    pcb_record_status_t status,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count);

/* Convenience wrapper: return all records oldest-to-newest. */
pcb_result_t pcb_get_all(
    const pcb_handle_t *handle,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count);

/* Convenience wrapper for PCB_STATUS_FAILED filtering. */
pcb_result_t pcb_get_failed(
    const pcb_handle_t *handle,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count);

/* Count records with a selected status without copying them. */
pcb_result_t pcb_count_by_status(
    const pcb_handle_t *handle,
    pcb_record_status_t status,
    size_t *out_count);

/* Clear RAM only. The empty state becomes persistent after Sync/DeInit. */
pcb_result_t pcb_clear(pcb_handle_t *handle);

size_t pcb_count(const pcb_handle_t *handle);
bool pcb_is_dirty(const pcb_handle_t *handle);
bool pcb_is_initialized(const pcb_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PERSISTENT_CIRCULAR_BUFFER_H */
