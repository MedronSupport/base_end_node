#include "persistent_circular_buffer.h"

#include "pcb_flash_port.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PCB_PAGE_A                    (0U)
#define PCB_PAGE_B                    (1U)
#define PCB_PAGE_NONE                 (0xFFU)

#define PCB_SNAPSHOT_HEADER_SIZE      (64U)
#define PCB_COMMIT_OFFSET             (56U)
#define PCB_PAYLOAD_OFFSET            (64U)

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;

    uint32_t generation;

    uint16_t count;
    uint16_t record_size;

    uint16_t next_record_id;
    uint16_t flags;

    uint32_t payload_crc32;
    uint32_t header_crc32;

    uint32_t reserved_word;
    uint8_t  reserved[24];

    uint64_t commit_marker;
} pcb_snapshot_header_t;

typedef struct
{
    uint8_t page_id;
    uint32_t page_address;
    pcb_snapshot_header_t header;
} pcb_snapshot_info_t;

_Static_assert(sizeof(pcb_record_t) == 16U,
               "pcb_record_t must be exactly 16 bytes");

_Static_assert(sizeof(pcb_snapshot_header_t) == PCB_SNAPSHOT_HEADER_SIZE,
               "Snapshot header must be exactly 64 bytes");

_Static_assert(offsetof(pcb_snapshot_header_t, commit_marker) == PCB_COMMIT_OFFSET,
               "Commit marker must start at byte 56");

_Static_assert((PCB_FLASH_PAGE_A_ADDRESS & 0x7UL) == 0UL,
               "Page A must be 8-byte aligned");

_Static_assert((PCB_FLASH_PAGE_B_ADDRESS & 0x7UL) == 0UL,
               "Page B must be 8-byte aligned");

_Static_assert(
    PCB_SNAPSHOT_HEADER_SIZE +
        (PCB_CAPACITY * sizeof(pcb_record_t)) <=
        PCB_FLASH_PAGE_SIZE_BYTES,
    "One snapshot must fit inside one Flash page");

static bool pcb_status_is_valid(pcb_record_status_t status)
{
    return (status == PCB_STATUS_FAILED) ||
           (status == PCB_STATUS_SENT);
}

static bool pcb_uuid_length_is_valid(uint8_t uuid_length)
{
    return (uuid_length == 4U) || (uuid_length == 7U);
}

static uint32_t pcb_page_address_from_id(uint8_t page_id)
{
    return (page_id == PCB_PAGE_A)
        ? PCB_FLASH_PAGE_A_ADDRESS
        : PCB_FLASH_PAGE_B_ADDRESS;
}

static uint32_t pcb_page_number_from_id(uint8_t page_id)
{
    return (page_id == PCB_PAGE_A)
        ? PCB_FLASH_PAGE_A_NUMBER
        : PCB_FLASH_PAGE_B_NUMBER;
}

static uint16_t pcb_physical_index_from_oldest(
    const pcb_handle_t *handle,
    uint16_t logical_index)
{
    const uint16_t oldest_index =
        (uint16_t)((handle->write_index + PCB_CAPACITY - handle->count) %
                   PCB_CAPACITY);

    return (uint16_t)((oldest_index + logical_index) % PCB_CAPACITY);
}

static uint16_t pcb_next_nonzero_record_id(uint16_t current)
{
    uint16_t next = (uint16_t)(current + 1U);

    if (next == 0U)
    {
        next = 1U;
    }

    return next;
}

static bool pcb_generation_is_newer(
    uint32_t candidate,
    uint32_t reference)
{
    return ((int32_t)(candidate - reference)) > 0;
}

/* IEEE CRC-32, reflected polynomial 0xEDB88320. */
static uint32_t pcb_crc32_update(
    uint32_t crc,
    const void *data,
    size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;

    for (size_t index = 0U; index < length; ++index)
    {
        crc ^= bytes[index];

        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask =
                (uint32_t)(-(int32_t)(crc & 1UL));

            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return crc;
}

static uint32_t pcb_crc32_begin(void)
{
    return 0xFFFFFFFFUL;
}

static uint32_t pcb_crc32_finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t pcb_calculate_header_crc(
    const pcb_snapshot_header_t *header)
{
    pcb_snapshot_header_t temporary = *header;
    temporary.header_crc32 = 0U;

    uint32_t crc = pcb_crc32_begin();
    crc = pcb_crc32_update(crc, &temporary, PCB_COMMIT_OFFSET);

    return pcb_crc32_finish(crc);
}

static uint32_t pcb_calculate_ram_payload_crc(
    const pcb_handle_t *handle)
{
    uint32_t crc = pcb_crc32_begin();

    for (uint16_t logical = 0U; logical < handle->count; ++logical)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        crc = pcb_crc32_update(
            crc,
            &handle->records[physical],
            sizeof(pcb_record_t));
    }

    return pcb_crc32_finish(crc);
}

static bool pcb_calculate_flash_payload_crc(
    uint32_t page_address,
    uint16_t count,
    uint32_t *out_crc)
{
    uint32_t crc = pcb_crc32_begin();

    for (uint16_t index = 0U; index < count; ++index)
    {
        pcb_record_t record;
        const uint32_t address =
            page_address +
            PCB_PAYLOAD_OFFSET +
            ((uint32_t)index * sizeof(pcb_record_t));

        if (pcb_flash_read(address, &record, sizeof(record)) != PCB_FLASH_OK)
        {
            return false;
        }

        crc = pcb_crc32_update(crc, &record, sizeof(record));
    }

    *out_crc = pcb_crc32_finish(crc);
    return true;
}

static bool pcb_record_is_valid(const pcb_record_t *record)
{
    return pcb_uuid_length_is_valid(record->uuid_length) &&
           pcb_status_is_valid((pcb_record_status_t)record->status) &&
           (record->record_id != 0U);
}

static bool pcb_snapshot_validate(
    uint8_t page_id,
    pcb_snapshot_info_t *out_info)
{
    const uint32_t page_address = pcb_page_address_from_id(page_id);
    pcb_snapshot_header_t header;

    if (pcb_flash_read(page_address, &header, sizeof(header)) != PCB_FLASH_OK)
    {
        return false;
    }

    if ((header.magic != PCB_SNAPSHOT_MAGIC) ||
        (header.format_version != PCB_RECORD_FORMAT_VERSION) ||
        (header.header_size != PCB_SNAPSHOT_HEADER_SIZE) ||
        (header.record_size != sizeof(pcb_record_t)) ||
        (header.count > PCB_CAPACITY) ||
        (header.next_record_id == 0U) ||
        (header.commit_marker != PCB_COMMIT_MARKER))
    {
        return false;
    }

    if (pcb_calculate_header_crc(&header) != header.header_crc32)
    {
        return false;
    }

    uint32_t payload_crc = 0U;

    if (!pcb_calculate_flash_payload_crc(
            page_address,
            header.count,
            &payload_crc))
    {
        return false;
    }

    if (payload_crc != header.payload_crc32)
    {
        return false;
    }

    for (uint16_t index = 0U; index < header.count; ++index)
    {
        pcb_record_t record;
        const uint32_t address =
            page_address +
            PCB_PAYLOAD_OFFSET +
            ((uint32_t)index * sizeof(pcb_record_t));

        if ((pcb_flash_read(address, &record, sizeof(record)) != PCB_FLASH_OK) ||
            !pcb_record_is_valid(&record))
        {
            return false;
        }
    }

    if (out_info != NULL)
    {
        out_info->page_id = page_id;
        out_info->page_address = page_address;
        out_info->header = header;
    }

    return true;
}

static bool pcb_load_snapshot_into_ram(
    pcb_handle_t *handle,
    const pcb_snapshot_info_t *snapshot)
{
    memset(handle->records, 0, sizeof(handle->records));

    for (uint16_t index = 0U; index < snapshot->header.count; ++index)
    {
        const uint32_t address =
            snapshot->page_address +
            PCB_PAYLOAD_OFFSET +
            ((uint32_t)index * sizeof(pcb_record_t));

        if (pcb_flash_read(
                address,
                &handle->records[index],
                sizeof(pcb_record_t)) != PCB_FLASH_OK)
        {
            return false;
        }
    }

    handle->count = snapshot->header.count;
    handle->write_index =
        (uint16_t)(snapshot->header.count % PCB_CAPACITY);
    handle->next_record_id = snapshot->header.next_record_id;
    handle->generation = snapshot->header.generation;
    handle->active_page = snapshot->page_id;
    handle->dirty = false;

    return true;
}

static pcb_result_t pcb_map_flash_result(pcb_flash_result_t result)
{
    switch (result)
    {
        case PCB_FLASH_OK:
            return PCB_OK;

        case PCB_FLASH_UNLOCK_ERROR:
            return PCB_ERROR_FLASH_UNLOCK;

        case PCB_FLASH_ERASE_ERROR:
            return PCB_ERROR_FLASH_ERASE;

        case PCB_FLASH_PROGRAM_ERROR:
            return PCB_ERROR_FLASH_PROGRAM;

        case PCB_FLASH_LOCK_ERROR:
            return PCB_ERROR_FLASH_LOCK;

        case PCB_FLASH_VERIFY_ERROR:
            return PCB_ERROR_FLASH_VERIFY;

        case PCB_FLASH_INVALID_ARGUMENT:
        default:
            return PCB_ERROR_INVALID_ARGUMENT;
    }
}

static pcb_result_t pcb_copy_recent_slice(
    const pcb_handle_t *handle,
    size_t skip_from_latest,
    size_t requested_count,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count)
{
    if ((handle == NULL) ||
        (out_count == NULL) ||
        ((output == NULL) && (output_capacity > 0U)))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    *out_count = 0U;

    if ((requested_count == 0U) ||
        (skip_from_latest >= handle->count))
    {
        return PCB_OK;
    }

    const size_t available =
        (size_t)handle->count - skip_from_latest;

    const size_t wanted =
        (requested_count < available)
            ? requested_count
            : available;

    const size_t copy_count =
        (wanted < output_capacity)
            ? wanted
            : output_capacity;

    const size_t selected_start =
        (size_t)handle->count - skip_from_latest - wanted;

    /* If truncated, keep the newest part of the selected slice. */
    const size_t first_to_copy =
        selected_start + (wanted - copy_count);

    for (size_t index = 0U; index < copy_count; ++index)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(
                handle,
                (uint16_t)(first_to_copy + index));

        output[index] = handle->records[physical];
    }

    *out_count = copy_count;

    return (copy_count < wanted)
        ? PCB_TRUNCATED
        : PCB_OK;
}

pcb_result_t pcb_init(pcb_handle_t *handle)
{
    if (handle == NULL)
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (handle->initialized)
    {
        return PCB_ERROR_ALREADY_INITIALIZED;
    }

    memset(handle, 0, sizeof(*handle));

    pcb_snapshot_info_t page_a = {0};
    pcb_snapshot_info_t page_b = {0};

    const bool page_a_valid = pcb_snapshot_validate(PCB_PAGE_A, &page_a);
    const bool page_b_valid = pcb_snapshot_validate(PCB_PAGE_B, &page_b);

    if (page_a_valid && page_b_valid)
    {
        const pcb_snapshot_info_t *selected =
            pcb_generation_is_newer(
                page_b.header.generation,
                page_a.header.generation)
                ? &page_b
                : &page_a;

        if (!pcb_load_snapshot_into_ram(handle, selected))
        {
            return PCB_ERROR_SNAPSHOT_CORRUPT;
        }
    }
    else if (page_a_valid)
    {
        if (!pcb_load_snapshot_into_ram(handle, &page_a))
        {
            return PCB_ERROR_SNAPSHOT_CORRUPT;
        }
    }
    else if (page_b_valid)
    {
        if (!pcb_load_snapshot_into_ram(handle, &page_b))
        {
            return PCB_ERROR_SNAPSHOT_CORRUPT;
        }
    }
    else
    {
        handle->write_index = 0U;
        handle->count = 0U;
        handle->next_record_id = 1U;
        handle->generation = 0U;
        handle->active_page = PCB_PAGE_NONE;
        handle->dirty = false;
    }

    handle->busy = false;
    handle->initialized = true;

    return PCB_OK;
}

pcb_result_t pcb_deinit(pcb_handle_t *handle)
{
    if (handle == NULL)
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->busy)
    {
        return PCB_ERROR_BUSY;
    }

    if (handle->dirty)
    {
        const pcb_result_t sync_result = pcb_sync(handle);

        if (sync_result != PCB_OK)
        {
            return sync_result;
        }
    }

    memset(handle, 0, sizeof(*handle));
    return PCB_OK;
}

pcb_result_t pcb_sync(pcb_handle_t *handle)
{
    if (handle == NULL)
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->busy)
    {
        return PCB_ERROR_BUSY;
    }

    if (!handle->dirty)
    {
        return PCB_OK;
    }

    handle->busy = true;

    const uint8_t target_page =
        (handle->active_page == PCB_PAGE_A)
            ? PCB_PAGE_B
            : PCB_PAGE_A;

    const uint32_t target_address =
        pcb_page_address_from_id(target_page);

    const uint32_t target_page_number =
        pcb_page_number_from_id(target_page);

    const uint32_t new_generation = handle->generation + 1UL;

    pcb_snapshot_header_t header;
    memset(&header, 0, sizeof(header));

    header.magic = PCB_SNAPSHOT_MAGIC;
    header.format_version = PCB_RECORD_FORMAT_VERSION;
    header.header_size = PCB_SNAPSHOT_HEADER_SIZE;
    header.generation = new_generation;
    header.count = handle->count;
    header.record_size = sizeof(pcb_record_t);
    header.next_record_id = handle->next_record_id;
    header.flags = 0U;
    header.payload_crc32 = pcb_calculate_ram_payload_crc(handle);
    header.header_crc32 = 0U;
    header.reserved_word = 0U;
    header.commit_marker = UINT64_MAX;
    header.header_crc32 = pcb_calculate_header_crc(&header);

    pcb_result_t result = PCB_OK;
    bool flash_unlocked = false;

    pcb_flash_result_t flash_result = pcb_flash_unlock();

    if (flash_result != PCB_FLASH_OK)
    {
        result = pcb_map_flash_result(flash_result);
        goto cleanup;
    }

    flash_unlocked = true;

    flash_result = pcb_flash_erase_page(target_page_number);

    if (flash_result != PCB_FLASH_OK)
    {
        result = pcb_map_flash_result(flash_result);
        goto cleanup;
    }

    /* Program bytes 0..55. Commit bytes 56..63 remain erased. */
    flash_result = pcb_flash_program(
        target_address,
        &header,
        PCB_COMMIT_OFFSET);

    if (flash_result != PCB_FLASH_OK)
    {
        result = pcb_map_flash_result(flash_result);
        goto cleanup;
    }

    for (uint16_t logical = 0U; logical < handle->count; ++logical)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        const uint32_t record_address =
            target_address +
            PCB_PAYLOAD_OFFSET +
            ((uint32_t)logical * sizeof(pcb_record_t));

        flash_result = pcb_flash_program(
            record_address,
            &handle->records[physical],
            sizeof(pcb_record_t));

        if (flash_result != PCB_FLASH_OK)
        {
            result = pcb_map_flash_result(flash_result);
            goto cleanup;
        }
    }

#if PCB_ENABLE_FULL_READBACK_VERIFY
    flash_result = pcb_flash_verify(
        target_address,
        &header,
        PCB_COMMIT_OFFSET);

    if (flash_result != PCB_FLASH_OK)
    {
        result = pcb_map_flash_result(flash_result);
        goto cleanup;
    }

    for (uint16_t logical = 0U; logical < handle->count; ++logical)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        const uint32_t record_address =
            target_address +
            PCB_PAYLOAD_OFFSET +
            ((uint32_t)logical * sizeof(pcb_record_t));

        flash_result = pcb_flash_verify(
            record_address,
            &handle->records[physical],
            sizeof(pcb_record_t));

        if (flash_result != PCB_FLASH_OK)
        {
            result = pcb_map_flash_result(flash_result);
            goto cleanup;
        }
    }
#endif

    {
        uint32_t flash_payload_crc = 0U;

        if (!pcb_calculate_flash_payload_crc(
                target_address,
                handle->count,
                &flash_payload_crc) ||
            (flash_payload_crc != header.payload_crc32))
        {
            result = PCB_ERROR_FLASH_VERIFY;
            goto cleanup;
        }
    }

    /* Commit the snapshot with one final, aligned double-word write. */
    {
        const uint64_t commit_marker = PCB_COMMIT_MARKER;

        flash_result = pcb_flash_program(
            target_address + PCB_COMMIT_OFFSET,
            &commit_marker,
            sizeof(commit_marker));

        if (flash_result != PCB_FLASH_OK)
        {
            result = pcb_map_flash_result(flash_result);
            goto cleanup;
        }

        flash_result = pcb_flash_verify(
            target_address + PCB_COMMIT_OFFSET,
            &commit_marker,
            sizeof(commit_marker));

        if (flash_result != PCB_FLASH_OK)
        {
            result = pcb_map_flash_result(flash_result);
            goto cleanup;
        }
    }

cleanup:
    if (flash_unlocked)
    {
        const pcb_flash_result_t lock_result = pcb_flash_lock();

        if ((result == PCB_OK) && (lock_result != PCB_FLASH_OK))
        {
            result = pcb_map_flash_result(lock_result);
        }
    }

    if (result == PCB_OK)
    {
        pcb_snapshot_info_t verification = {0};

        if (!pcb_snapshot_validate(target_page, &verification))
        {
            result = PCB_ERROR_SNAPSHOT_CORRUPT;
        }
        else
        {
            handle->generation = new_generation;
            handle->active_page = target_page;
            handle->dirty = false;
        }
    }

    handle->busy = false;
    return result;
}

pcb_result_t pcb_add(
    pcb_handle_t *handle,
    uint32_t timestamp,
    const uint8_t *uuid,
    uint8_t uuid_length,
    pcb_record_status_t status,
    uint16_t *out_record_id)
{
    if ((handle == NULL) ||
        (uuid == NULL) ||
        !pcb_uuid_length_is_valid(uuid_length) ||
        !pcb_status_is_valid(status))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->busy)
    {
        return PCB_ERROR_BUSY;
    }

    pcb_record_t *record = &handle->records[handle->write_index];
    memset(record, 0, sizeof(*record));

    record->timestamp = timestamp;
    memcpy(record->uuid, uuid, uuid_length);
    record->uuid_length = uuid_length;
    record->record_id = handle->next_record_id;
    record->status = (uint8_t)status;
    record->reserved = 0U;

    if (out_record_id != NULL)
    {
        *out_record_id = record->record_id;
    }

    handle->next_record_id =
        pcb_next_nonzero_record_id(handle->next_record_id);

    handle->write_index =
        (uint16_t)((handle->write_index + 1U) % PCB_CAPACITY);

    if (handle->count < PCB_CAPACITY)
    {
        ++handle->count;
    }

    handle->dirty = true;
    return PCB_OK;
}

pcb_result_t pcb_set_status_by_id(
    pcb_handle_t *handle,
    uint16_t record_id,
    pcb_record_status_t new_status)
{
    if ((handle == NULL) ||
        (record_id == 0U) ||
        !pcb_status_is_valid(new_status))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->busy)
    {
        return PCB_ERROR_BUSY;
    }

    for (uint16_t logical = 0U; logical < handle->count; ++logical)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        pcb_record_t *record = &handle->records[physical];

        if (record->record_id == record_id)
        {
            if (record->status != (uint8_t)new_status)
            {
                record->status = (uint8_t)new_status;
                handle->dirty = true;
            }

            return PCB_OK;
        }
    }

    return PCB_ERROR_NOT_FOUND;
}

pcb_result_t pcb_set_status_by_key(
    pcb_handle_t *handle,
    uint32_t timestamp,
    const uint8_t *uuid,
    uint8_t uuid_length,
    pcb_record_status_t new_status)
{
    if ((handle == NULL) ||
        (uuid == NULL) ||
        !pcb_uuid_length_is_valid(uuid_length) ||
        !pcb_status_is_valid(new_status))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->busy)
    {
        return PCB_ERROR_BUSY;
    }

    for (uint16_t offset = 0U; offset < handle->count; ++offset)
    {
        const uint16_t logical =
            (uint16_t)(handle->count - 1U - offset);

        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        pcb_record_t *record = &handle->records[physical];

        if ((record->timestamp == timestamp) &&
            (record->uuid_length == uuid_length) &&
            (memcmp(record->uuid, uuid, uuid_length) == 0))
        {
            if (record->status != (uint8_t)new_status)
            {
                record->status = (uint8_t)new_status;
                handle->dirty = true;
            }

            return PCB_OK;
        }
    }

    return PCB_ERROR_NOT_FOUND;
}

pcb_result_t pcb_get_latest(
    const pcb_handle_t *handle,
    pcb_record_t *out_record)
{
    if ((handle == NULL) || (out_record == NULL))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->count == 0U)
    {
        return PCB_ERROR_EMPTY;
    }

    const uint16_t newest =
        (uint16_t)((handle->write_index + PCB_CAPACITY - 1U) %
                   PCB_CAPACITY);

    *out_record = handle->records[newest];
    return PCB_OK;
}

pcb_result_t pcb_get_latest_by_status(
    const pcb_handle_t *handle,
    pcb_record_status_t status,
    pcb_record_t *out_record)
{
    if ((handle == NULL) ||
        (out_record == NULL) ||
        !pcb_status_is_valid(status))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->count == 0U)
    {
        return PCB_ERROR_EMPTY;
    }

    for (uint16_t offset = 0U; offset < handle->count; ++offset)
    {
        const uint16_t logical =
            (uint16_t)(handle->count - 1U - offset);

        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        const pcb_record_t *record = &handle->records[physical];

        if (record->status == (uint8_t)status)
        {
            *out_record = *record;
            return PCB_OK;
        }
    }

    return PCB_ERROR_NOT_FOUND;
}

pcb_result_t pcb_get_last_n(
    const pcb_handle_t *handle,
    size_t requested_count,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count)
{
    return pcb_copy_recent_slice(
        handle,
        0U,
        requested_count,
        output,
        output_capacity,
        out_count);
}

pcb_result_t pcb_get_recent_slice(
    const pcb_handle_t *handle,
    size_t skip_from_latest,
    size_t requested_count,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count)
{
    return pcb_copy_recent_slice(
        handle,
        skip_from_latest,
        requested_count,
        output,
        output_capacity,
        out_count);
}

pcb_result_t pcb_get_by_timestamp(
    const pcb_handle_t *handle,
    uint32_t start_timestamp,
    uint32_t end_timestamp,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count)
{
    if ((handle == NULL) ||
        (out_count == NULL) ||
        ((output == NULL) && (output_capacity > 0U)) ||
        (start_timestamp > end_timestamp))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    size_t copied = 0U;
    size_t matched = 0U;

    for (uint16_t logical = 0U; logical < handle->count; ++logical)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        const pcb_record_t *record = &handle->records[physical];

        if ((record->timestamp >= start_timestamp) &&
            (record->timestamp <= end_timestamp))
        {
            if (copied < output_capacity)
            {
                output[copied] = *record;
                ++copied;
            }

            ++matched;
        }
    }

    *out_count = copied;

    return (copied < matched)
        ? PCB_TRUNCATED
        : PCB_OK;
}

pcb_result_t pcb_get_by_status(
    const pcb_handle_t *handle,
    pcb_record_status_t status,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count)
{
    if ((handle == NULL) ||
        (out_count == NULL) ||
        ((output == NULL) && (output_capacity > 0U)) ||
        !pcb_status_is_valid(status))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    size_t copied = 0U;
    size_t matched = 0U;

    for (uint16_t logical = 0U; logical < handle->count; ++logical)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        const pcb_record_t *record = &handle->records[physical];

        if (record->status == (uint8_t)status)
        {
            if (copied < output_capacity)
            {
                output[copied] = *record;
                ++copied;
            }

            ++matched;
        }
    }

    *out_count = copied;

    return (copied < matched)
        ? PCB_TRUNCATED
        : PCB_OK;
}

pcb_result_t pcb_get_all(
    const pcb_handle_t *handle,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count)
{
    if (handle == NULL)
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    return pcb_get_last_n(
        handle,
        handle->count,
        output,
        output_capacity,
        out_count);
}

pcb_result_t pcb_get_failed(
    const pcb_handle_t *handle,
    pcb_record_t *output,
    size_t output_capacity,
    size_t *out_count)
{
    return pcb_get_by_status(
        handle,
        PCB_STATUS_FAILED,
        output,
        output_capacity,
        out_count);
}

pcb_result_t pcb_count_by_status(
    const pcb_handle_t *handle,
    pcb_record_status_t status,
    size_t *out_count)
{
    if ((handle == NULL) ||
        (out_count == NULL) ||
        !pcb_status_is_valid(status))
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    size_t count = 0U;

    for (uint16_t logical = 0U; logical < handle->count; ++logical)
    {
        const uint16_t physical =
            pcb_physical_index_from_oldest(handle, logical);

        if (handle->records[physical].status == (uint8_t)status)
        {
            ++count;
        }
    }

    *out_count = count;
    return PCB_OK;
}

pcb_result_t pcb_clear(pcb_handle_t *handle)
{
    if (handle == NULL)
    {
        return PCB_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->initialized)
    {
        return PCB_ERROR_NOT_INITIALIZED;
    }

    if (handle->busy)
    {
        return PCB_ERROR_BUSY;
    }

    memset(handle->records, 0, sizeof(handle->records));
    handle->write_index = 0U;
    handle->count = 0U;
    handle->dirty = true;

    return PCB_OK;
}

size_t pcb_count(const pcb_handle_t *handle)
{
    if ((handle == NULL) || !handle->initialized)
    {
        return 0U;
    }

    return handle->count;
}

bool pcb_is_dirty(const pcb_handle_t *handle)
{
    return (handle != NULL) &&
           handle->initialized &&
           handle->dirty;
}

bool pcb_is_initialized(const pcb_handle_t *handle)
{
    return (handle != NULL) && handle->initialized;
}
