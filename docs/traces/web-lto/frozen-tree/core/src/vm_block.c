/*
 * S5LBox -- portable, bounded random-access block backend.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "vm_block.h"

vm_block_status_t vm_block_check_range(const vm_block_t *block,
                                       uint64_t offset,
                                       uint64_t length) {
    if (!block)
        return VM_BLOCK_STATUS_INVALID_ARGUMENT;

    if (length > UINT64_MAX - offset)
        return VM_BLOCK_STATUS_OVERFLOW;
    if (offset > block->size || length > block->size - offset)
        return VM_BLOCK_STATUS_BOUNDS;

    return VM_BLOCK_STATUS_OK;
}

vm_block_status_t vm_block_get_info(const vm_block_t *block,
                                    vm_block_info_t *out_info) {
    if (!block || !out_info)
        return VM_BLOCK_STATUS_INVALID_ARGUMENT;

    out_info->size = block->size;
    out_info->identity = block->identity;
    out_info->generation = block->generation;
    return VM_BLOCK_STATUS_OK;
}

vm_block_status_t vm_block_read_exact(const vm_block_t *block,
                                      uint64_t offset,
                                      void *destination,
                                      size_t length,
                                      vm_block_cancel_fn cancelled,
                                      void *cancel_context,
                                      size_t *out_transferred) {
    size_t completed = 0;
    uint8_t *bytes = (uint8_t *)destination;
    unsigned no_progress = 0;
    vm_block_status_t range_status;

    if (out_transferred)
        *out_transferred = 0;
    if (!block)
        return VM_BLOCK_STATUS_INVALID_ARGUMENT;

#if SIZE_MAX > UINT64_MAX
    if (length > (size_t)UINT64_MAX)
        return VM_BLOCK_STATUS_OVERFLOW;
#endif

    range_status = vm_block_check_range(block, offset, (uint64_t)length);
    if (range_status != VM_BLOCK_STATUS_OK)
        return range_status;

    if (length == 0)
        return VM_BLOCK_STATUS_OK;
    if (!destination || !block->read_at)
        return VM_BLOCK_STATUS_INVALID_ARGUMENT;

    while (completed < length) {
        size_t actual = 0;
        size_t remaining = length - completed;
        size_t requested = remaining;
        vm_block_io_status_t io_status;

        if (requested > VM_BLOCK_MAX_CALLBACK_BYTES)
            requested = (size_t)VM_BLOCK_MAX_CALLBACK_BYTES;

        if (cancelled && cancelled(cancel_context)) {
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_CANCELLED;
        }

        io_status = block->read_at(block->context,
                                   offset + (uint64_t)completed,
                                   bytes + completed,
                                   requested,
                                   &actual);

        if (actual > requested) {
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_PROTOCOL;
        }

        switch (io_status) {
        case VM_BLOCK_IO_OK:
            if (actual != 0) {
                completed += actual;
                no_progress = 0;
                break;
            }
            no_progress++;
            break;

        case VM_BLOCK_IO_RETRY:
            if (actual != 0) {
                if (out_transferred)
                    *out_transferred = completed;
                return VM_BLOCK_STATUS_PROTOCOL;
            }
            no_progress++;
            break;

        case VM_BLOCK_IO_ERROR:
            if (actual != 0) {
                if (out_transferred)
                    *out_transferred = completed;
                return VM_BLOCK_STATUS_PROTOCOL;
            }
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_BACKEND;

        default:
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_PROTOCOL;
        }

        if (no_progress >= VM_BLOCK_NO_PROGRESS_LIMIT) {
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_STALLED;
        }
    }

    if (out_transferred)
        *out_transferred = completed;
    return VM_BLOCK_STATUS_OK;
}

vm_block_status_t vm_block_write_exact(const vm_block_t *block,
                                       uint64_t offset,
                                       const void *source,
                                       size_t length,
                                       vm_block_cancel_fn cancelled,
                                       void *cancel_context,
                                       size_t *out_transferred) {
    size_t completed = 0;
    const uint8_t *bytes = (const uint8_t *)source;
    unsigned no_progress = 0;
    vm_block_status_t range_status;

    if (out_transferred)
        *out_transferred = 0;
    if (!block)
        return VM_BLOCK_STATUS_INVALID_ARGUMENT;

#if SIZE_MAX > UINT64_MAX
    if (length > (size_t)UINT64_MAX)
        return VM_BLOCK_STATUS_OVERFLOW;
#endif

    range_status = vm_block_check_range(block, offset, (uint64_t)length);
    if (range_status != VM_BLOCK_STATUS_OK)
        return range_status;

    if (length == 0)
        return VM_BLOCK_STATUS_OK;
    if (!source || !block->write_at)
        return VM_BLOCK_STATUS_INVALID_ARGUMENT;

    while (completed < length) {
        size_t actual = 0;
        size_t remaining = length - completed;
        size_t requested = remaining;
        vm_block_io_status_t io_status;

        if (requested > VM_BLOCK_MAX_CALLBACK_BYTES)
            requested = (size_t)VM_BLOCK_MAX_CALLBACK_BYTES;

        if (cancelled && cancelled(cancel_context)) {
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_CANCELLED;
        }

        io_status = block->write_at(block->context,
                                    offset + (uint64_t)completed,
                                    bytes + completed,
                                    requested,
                                    &actual);

        if (actual > requested) {
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_PROTOCOL;
        }

        switch (io_status) {
        case VM_BLOCK_IO_OK:
            if (actual != 0) {
                completed += actual;
                no_progress = 0;
                break;
            }
            no_progress++;
            break;

        case VM_BLOCK_IO_RETRY:
            if (actual != 0) {
                if (out_transferred)
                    *out_transferred = completed;
                return VM_BLOCK_STATUS_PROTOCOL;
            }
            no_progress++;
            break;

        case VM_BLOCK_IO_ERROR:
            if (actual != 0) {
                if (out_transferred)
                    *out_transferred = completed;
                return VM_BLOCK_STATUS_PROTOCOL;
            }
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_BACKEND;

        default:
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_PROTOCOL;
        }

        if (no_progress >= VM_BLOCK_NO_PROGRESS_LIMIT) {
            if (out_transferred)
                *out_transferred = completed;
            return VM_BLOCK_STATUS_STALLED;
        }
    }

    if (out_transferred)
        *out_transferred = completed;
    return VM_BLOCK_STATUS_OK;
}

vm_block_status_t vm_block_flush(const vm_block_t *block,
                                 vm_block_cancel_fn cancelled,
                                 void *cancel_context) {
    unsigned no_progress = 0;

    if (!block || !block->flush)
        return VM_BLOCK_STATUS_INVALID_ARGUMENT;

    for (;;) {
        vm_block_io_status_t io_status;

        if (cancelled && cancelled(cancel_context))
            return VM_BLOCK_STATUS_CANCELLED;

        io_status = block->flush(block->context);
        switch (io_status) {
        case VM_BLOCK_IO_OK:
            return VM_BLOCK_STATUS_OK;
        case VM_BLOCK_IO_RETRY:
            no_progress++;
            if (no_progress >= VM_BLOCK_NO_PROGRESS_LIMIT)
                return VM_BLOCK_STATUS_STALLED;
            break;
        case VM_BLOCK_IO_ERROR:
            return VM_BLOCK_STATUS_BACKEND;
        default:
            return VM_BLOCK_STATUS_PROTOCOL;
        }
    }
}

const char *vm_block_strerror(vm_block_status_t status) {
    switch (status) {
    case VM_BLOCK_STATUS_OK:               return "success";
    case VM_BLOCK_STATUS_INVALID_ARGUMENT: return "invalid argument";
    case VM_BLOCK_STATUS_OVERFLOW:         return "block range arithmetic overflow";
    case VM_BLOCK_STATUS_BOUNDS:           return "range outside block";
    case VM_BLOCK_STATUS_BACKEND:          return "block backend I/O error";
    case VM_BLOCK_STATUS_CANCELLED:        return "operation cancelled";
    case VM_BLOCK_STATUS_STALLED:          return "block backend made no progress";
    case VM_BLOCK_STATUS_PROTOCOL:         return "block callback protocol violation";
    default:                               return "unknown block status";
    }
}
