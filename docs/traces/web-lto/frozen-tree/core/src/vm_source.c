/*
 * S5LBox -- portable, random-access byte source.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "vm_source.h"

vm_source_status_t vm_source_check_range(const vm_source_t *source,
                                         uint64_t offset,
                                         uint64_t length) {
    if (!source)
        return VM_SOURCE_STATUS_INVALID_ARGUMENT;

    /* Subtraction is safe only after proving offset is inside the source. */
    if (offset > source->size || length > source->size - offset)
        return VM_SOURCE_STATUS_RANGE;

    return VM_SOURCE_STATUS_OK;
}

vm_source_status_t vm_source_read_exact(const vm_source_t *source,
                                        uint64_t offset,
                                        void *destination,
                                        size_t length,
                                        vm_source_cancel_fn cancelled,
                                        void *cancel_context,
                                        size_t *out_read) {
    size_t completed = 0;
    uint8_t *bytes = (uint8_t *)destination;
    vm_source_status_t range_status;

    if (out_read)
        *out_read = 0;
    if (!source)
        return VM_SOURCE_STATUS_INVALID_ARGUMENT;

#if SIZE_MAX > UINT64_MAX
    if (length > (size_t)UINT64_MAX)
        return VM_SOURCE_STATUS_RANGE;
#endif

    range_status = vm_source_check_range(source, offset, (uint64_t)length);
    if (range_status != VM_SOURCE_STATUS_OK)
        return range_status;

    if (length == 0)
        return VM_SOURCE_STATUS_OK;
    if (!destination || !source->read_at)
        return VM_SOURCE_STATUS_INVALID_ARGUMENT;

    while (completed < length) {
        size_t actual = 0;
        size_t remaining = length - completed;
        vm_source_io_status_t io_status;

        if (cancelled && cancelled(cancel_context)) {
            if (out_read)
                *out_read = completed;
            return VM_SOURCE_STATUS_CANCELLED;
        }

        io_status = source->read_at(source->context,
                                    offset + (uint64_t)completed,
                                    bytes + completed,
                                    remaining,
                                    &actual);

        if (actual > remaining) {
            if (out_read)
                *out_read = completed;
            return VM_SOURCE_STATUS_PROTOCOL;
        }

        switch (io_status) {
        case VM_SOURCE_IO_OK:
            if (actual == 0) {
                if (out_read)
                    *out_read = completed;
                return VM_SOURCE_STATUS_EOF;
            }
            completed += actual;
            break;

        case VM_SOURCE_IO_RETRY:
            if (actual != 0) {
                if (out_read)
                    *out_read = completed;
                return VM_SOURCE_STATUS_PROTOCOL;
            }
            break;

        case VM_SOURCE_IO_ERROR:
            if (actual != 0) {
                if (out_read)
                    *out_read = completed;
                return VM_SOURCE_STATUS_PROTOCOL;
            }
            if (out_read)
                *out_read = completed;
            return VM_SOURCE_STATUS_IO;

        default:
            if (out_read)
                *out_read = completed;
            return VM_SOURCE_STATUS_PROTOCOL;
        }
    }

    if (out_read)
        *out_read = completed;
    return VM_SOURCE_STATUS_OK;
}

const char *vm_source_strerror(vm_source_status_t status) {
    switch (status) {
    case VM_SOURCE_STATUS_OK:               return "success";
    case VM_SOURCE_STATUS_INVALID_ARGUMENT: return "invalid argument";
    case VM_SOURCE_STATUS_RANGE:            return "range outside source";
    case VM_SOURCE_STATUS_EOF:              return "unexpected end of source";
    case VM_SOURCE_STATUS_IO:               return "source I/O error";
    case VM_SOURCE_STATUS_CANCELLED:        return "operation cancelled";
    case VM_SOURCE_STATUS_PROTOCOL:         return "source callback protocol violation";
    default:                                return "unknown source status";
    }
}
