/*
 * S5LBox -- portable, bounded random-access block backend.
 *
 * Mutable guest media must not make the portable core depend on FILE,
 * descriptors, errno, Objective-C objects, or any other host API.  Host
 * frontends implement these callbacks and translate their native errors at
 * this boundary.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_VM_BLOCK_H
#define S5LBOX_VM_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * One host callback result.  OK may report a positive partial transfer.
 * RETRY represents a transient interruption and ERROR a backend failure; both
 * must leave *actual equal to zero and must not modify the read destination or
 * writable backing store.  An adapter whose host API reports both partial
 * progress and an OS error must return OK with that progress, then surface the
 * pending RETRY or ERROR on the next callback.  It must never combine progress
 * with RETRY or ERROR.  The core never reads a host error value.
 */
typedef enum {
    VM_BLOCK_IO_OK = 0,
    VM_BLOCK_IO_RETRY,
    VM_BLOCK_IO_ERROR
} vm_block_io_status_t;

typedef vm_block_io_status_t (*vm_block_read_at_fn)(
    void *context,
    uint64_t offset,
    void *destination,
    size_t requested,
    size_t *actual);

typedef vm_block_io_status_t (*vm_block_write_at_fn)(
    void *context,
    uint64_t offset,
    const void *source,
    size_t requested,
    size_t *actual);

typedef vm_block_io_status_t (*vm_block_flush_fn)(void *context);

/* Return true when the in-progress operation should stop. */
typedef bool (*vm_block_cancel_fn)(void *context);

/*
 * identity identifies the backing object while generation identifies the
 * exact incarnation/content epoch exposed to a VM.  These are caller-supplied
 * tokens: this layer neither derives nor advances them.  Hosts should change
 * generation when replacing or incompatibly resetting media with the same
 * identity.  All three metadata values are deliberately 64-bit.
 *
 * A zero identity or generation means that token is unavailable.  Snapshot
 * code may bind a block only when both tokens are nonzero, frozen for the VM
 * session, and revalidated on restore.  Zero or unfrozen/externally mutable
 * metadata is deliberately unbindable and must not be treated as snapshot
 * identity proof.
 *
 * The descriptor, callback pointers, context, metadata, and backing object
 * must remain alive and stable for the complete operation.  Calls and any
 * external mutation of those objects must be serialized by the caller unless
 * the host adapter supplies stronger synchronization itself.
 */
typedef struct {
    void *context;
    uint64_t size;
    uint64_t identity;
    uint64_t generation;
    vm_block_read_at_fn read_at;
    vm_block_write_at_fn write_at;
    vm_block_flush_fn flush;
} vm_block_t;

typedef struct {
    uint64_t size;
    uint64_t identity;
    uint64_t generation;
} vm_block_info_t;

typedef enum {
    VM_BLOCK_STATUS_OK = 0,
    VM_BLOCK_STATUS_INVALID_ARGUMENT,
    VM_BLOCK_STATUS_OVERFLOW,
    VM_BLOCK_STATUS_BOUNDS,
    VM_BLOCK_STATUS_BACKEND,
    VM_BLOCK_STATUS_CANCELLED,
    VM_BLOCK_STATUS_STALLED,
    VM_BLOCK_STATUS_PROTOCOL
} vm_block_status_t;

/*
 * Consecutive RETRY or successful zero-byte callbacks are bounded so a broken
 * or permanently unavailable backend cannot spin the emulator forever.  Any
 * positive transfer resets this counter.
 */
#define VM_BLOCK_NO_PROGRESS_LIMIT 64u

/*
 * Each read_at/write_at request is capped at 1 MiB even when the exact
 * operation is larger.  Besides keeping cancellation responsive, this gives
 * adapters a portable bound below DWORD and every supported SSIZE_MAX.
 */
#define VM_BLOCK_MAX_CALLBACK_BYTES UINT32_C(1048576)

/*
 * Validate [offset, offset + length).  Arithmetic overflow is distinguished
 * from a representable range outside the block.  A zero-length range exactly
 * at block->size is valid.
 */
vm_block_status_t vm_block_check_range(const vm_block_t *block,
                                       uint64_t offset,
                                       uint64_t length);

/* Copy immutable metadata without exposing callback or host context fields. */
vm_block_status_t vm_block_get_info(const vm_block_t *block,
                                    vm_block_info_t *out_info);

/*
 * Transfer exactly length bytes, accepting positive partial transfers and
 * transient retries.  Cancellation is polled before every host callback.
 * out_transferred, when non-NULL, receives committed progress on every exit.
 * It must not alias the block descriptor, its context/backing storage, or any
 * part of the source/destination buffer; this function writes it at entry and
 * again after callbacks.  The transfer buffer, block callback context,
 * cancellation callback, and cancellation context must stay valid until the
 * function returns.  Any concurrent access or mutation must be synchronized
 * by the caller or adapter.
 *
 * A zero-length operation performs no callback and permits a NULL buffer and
 * a missing callback.  Non-empty reads/writes require the corresponding
 * callback.  STALLED means VM_BLOCK_NO_PROGRESS_LIMIT consecutive callbacks
 * returned RETRY or OK with zero bytes.  Every callback request is in
 * [1, VM_BLOCK_MAX_CALLBACK_BYTES].
 */
vm_block_status_t vm_block_read_exact(const vm_block_t *block,
                                      uint64_t offset,
                                      void *destination,
                                      size_t length,
                                      vm_block_cancel_fn cancelled,
                                      void *cancel_context,
                                      size_t *out_transferred);

vm_block_status_t vm_block_write_exact(const vm_block_t *block,
                                       uint64_t offset,
                                       const void *source,
                                       size_t length,
                                       vm_block_cancel_fn cancelled,
                                       void *cancel_context,
                                       size_t *out_transferred);

/*
 * Flush all prior writes.  A flush callback is required: omitting it is not
 * silently treated as durable storage.  RETRY is subject to the same bounded
 * no-progress policy, and cancellation is polled before each attempt.  The
 * descriptor and both callback contexts follow the same lifetime and
 * serialization rules described above.
 */
vm_block_status_t vm_block_flush(const vm_block_t *block,
                                 vm_block_cancel_fn cancelled,
                                 void *cancel_context);

const char *vm_block_strerror(vm_block_status_t status);

#endif /* S5LBOX_VM_BLOCK_H */
