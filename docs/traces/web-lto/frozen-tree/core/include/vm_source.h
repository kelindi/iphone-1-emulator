/*
 * S5LBox -- portable, random-access byte source.
 *
 * Large guest inputs must not be copied wholesale into RAM.  This interface
 * gives the portable core a small pread-like boundary without exposing FILE,
 * file descriptors, errno, Objective-C objects, or any other host API.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_VM_SOURCE_H
#define S5LBOX_VM_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Result of one host callback invocation.  VM_SOURCE_IO_RETRY represents a
 * transient interruption (for example EINTR), but the callback performs that
 * platform-specific translation: the core itself never reads errno.
 *
 * VM_SOURCE_IO_OK may report a positive short read.  For RETRY and ERROR,
 * *actual must remain zero.
 */
typedef enum {
    VM_SOURCE_IO_OK = 0,
    VM_SOURCE_IO_RETRY,
    VM_SOURCE_IO_ERROR
} vm_source_io_status_t;

typedef vm_source_io_status_t (*vm_source_read_at_fn)(
    void *context,
    uint64_t offset,
    void *destination,
    size_t requested,
    size_t *actual);

/* Return true when the in-progress operation should stop. */
typedef bool (*vm_source_cancel_fn)(void *context);

typedef struct {
    void *context;
    uint64_t size;
    vm_source_read_at_fn read_at;
} vm_source_t;

typedef enum {
    VM_SOURCE_STATUS_OK = 0,
    VM_SOURCE_STATUS_INVALID_ARGUMENT,
    VM_SOURCE_STATUS_RANGE,
    VM_SOURCE_STATUS_EOF,
    VM_SOURCE_STATUS_IO,
    VM_SOURCE_STATUS_CANCELLED,
    VM_SOURCE_STATUS_PROTOCOL
} vm_source_status_t;

/*
 * Validate the half-open range [offset, offset + length) without overflowing.
 * A zero-length range at source->size is valid.
 */
vm_source_status_t vm_source_check_range(const vm_source_t *source,
                                         uint64_t offset,
                                         uint64_t length);

/*
 * Fill exactly length bytes, accepting positive short reads and retrying
 * transient interruptions.  An OK callback result with *actual == 0 before
 * completion is reported as EOF so a broken/truncated source cannot spin.
 *
 * Cancellation is polled before every host callback.  When out_read is not
 * NULL, it receives the number of bytes completed even on EOF, I/O error, or
 * cancellation.  A zero-length read performs no callback and permits a NULL
 * destination and a source with no read_at callback.
 */
vm_source_status_t vm_source_read_exact(const vm_source_t *source,
                                        uint64_t offset,
                                        void *destination,
                                        size_t length,
                                        vm_source_cancel_fn cancelled,
                                        void *cancel_context,
                                        size_t *out_read);

const char *vm_source_strerror(vm_source_status_t status);

#endif /* S5LBOX_VM_SOURCE_H */
