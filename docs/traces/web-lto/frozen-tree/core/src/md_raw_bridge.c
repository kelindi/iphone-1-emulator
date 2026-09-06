/*
 * S5LBox -- bounded host-backed XNU32 raw memory-disk bridge.
 *
 * The patched entry ABI is the ordinary Darwin character-device ABI:
 *   r0 = dev_t, r1 = struct uio *, r2 = ioflag (ignored), r14 = return PC.
 * The exact patch is `svc #0xe3; svc #0xe4`. Direct results skip the second
 * halfword and redirect to the saved LR. A user translation/permission fault
 * redirects to native uiomove, which returns to the second SVC. That bounded
 * continuation is keyed by the entry kernel SP.
 */
#include "md_raw_bridge.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MD_RAW_ADDRESS_SPACE_SIZE (UINT64_C(1) << 32)
#define XNU32_UIO_IOVS UINT32_C(0x00)
#define XNU32_UIO_IOVCNT UINT32_C(0x04)
#define XNU32_UIO_OFFSET_LO UINT32_C(0x08)
#define XNU32_UIO_OFFSET_HI UINT32_C(0x0c)
#define XNU32_UIO_SEGFLG UINT32_C(0x10)
#define XNU32_UIO_RW UINT32_C(0x14)
#define XNU32_UIO_RESID UINT32_C(0x18)
#define XNU32_UIO_MAX_IOVS UINT32_C(0x20)
#define XNU32_UIO_FLAGS UINT32_C(0x24)
#define XNU32_UIO_FLAGS_INITED UINT32_C(0x00000001)

#define XNU32_UIO_READ UINT32_C(0)
#define XNU32_UIO_WRITE UINT32_C(1)

#define XNU32_UIO_USERSPACE UINT32_C(0)
#define XNU32_UIO_USERISPACE UINT32_C(1)
#define XNU32_UIO_PHYS_USERSPACE UINT32_C(3)
#define XNU32_UIO_USERSPACE32 UINT32_C(5)
#define XNU32_UIO_USERISPACE32 UINT32_C(6)
#define XNU32_UIO_PHYS_USERSPACE32 UINT32_C(7)
#define XNU32_UIO_USERSPACE64 UINT32_C(8)
#define XNU32_UIO_USERISPACE64 UINT32_C(9)
#define XNU32_UIO_PHYS_USERSPACE64 UINT32_C(10)

#define XNU_ERR_ENXIO 6
#define XNU_ERR_EFAULT 14
#define XNU_ERR_EBUSY 16
#define XNU_ERR_EINVAL 22

typedef struct {
    uint32_t va;
    uint32_t pa;
    uint32_t length;
} mapped_span_t;

typedef enum {
    MAP_OK = 0,
    MAP_ADDRESS,
    MAP_TRANSLATION,
    MAP_RANGE,
    MAP_ALIAS,
    MAP_PENDING_ALIAS,
    MAP_BOUNCE,
    MAP_CAPACITY
} map_status_t;

static uint64_t add_saturating_u64(uint64_t a, uint64_t b) {
    return UINT64_MAX - a < b ? UINT64_MAX : a + b;
}

static void increment_saturating_u64(uint64_t *value) {
    if (*value != UINT64_MAX)
        (*value)++;
}

static bool range_end_32(uint64_t base, uint64_t size, uint64_t *end) {
    if (size == 0u || base >= MD_RAW_ADDRESS_SPACE_SIZE ||
        size > MD_RAW_ADDRESS_SPACE_SIZE - base)
        return false;
    *end = base + size;
    return true;
}

static bool range_contains(uint64_t base, uint64_t size,
                           uint64_t address, uint64_t length) {
    if (address < base || length > size)
        return false;
    return address - base <= size - length;
}

static bool ranges_overlap(uint64_t a, uint64_t a_length,
                           uint64_t b, uint64_t b_length) {
    return a < b + b_length && b < a + a_length;
}

static uint32_t load_u32(const uint8_t *bytes) {
    uint32_t value = bytes[0];
    value |= (uint32_t)bytes[1] << 8;
    value |= (uint32_t)bytes[2] << 16;
    value |= (uint32_t)bytes[3] << 24;
    return value;
}

static void store_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool site_valid(const md_raw_bridge_site_t *site) {
    return site->pc <= UINT32_MAX - 3u && (site->pc & 1u) == 0u &&
           site->encoding <= UINT16_MAX &&
           (site->encoding & UINT32_C(0xff00)) == UINT32_C(0xdf00);
}

bool md_raw_bridge_config_valid(const md_raw_bridge_config_t *config) {
    uint64_t ram_end;
    uint64_t bounce_length;

    if (config == NULL || config->ram == NULL || config->block == NULL ||
        config->block->read_at == NULL || config->block->write_at == NULL ||
        !site_valid(&config->site) ||
        !site_valid(&config->completion_site))
        return false;
    if (config->site.encoding != UINT32_C(0xdfe3) ||
        config->site.pc > UINT32_MAX - 2u ||
        config->completion_site.pc != config->site.pc + 2u ||
        config->completion_site.encoding != UINT32_C(0xdfe4) ||
        config->uiomove_thumb_pc == 0u ||
        (config->uiomove_thumb_pc & 1u) != 0u)
        return false;
    if (config->media_size == 0u || config->media_size > INT64_MAX ||
        config->block->size != config->media_size)
        return false;
    if (config->user_address_limit == 0u ||
        (config->user_address_limit &
         (MD_RAW_BRIDGE_PAGE_SIZE - UINT32_C(1))) != 0u)
        return false;
    if ((config->ram_base & (MD_RAW_BRIDGE_PAGE_SIZE - UINT64_C(1))) != 0u ||
        (config->ram_size & (MD_RAW_BRIDGE_PAGE_SIZE - UINT64_C(1))) != 0u)
        return false;
#if SIZE_MAX < UINT64_MAX
    if (config->ram_size > (uint64_t)SIZE_MAX)
        return false;
#endif
    if (!range_end_32(config->ram_base, config->ram_size, &ram_end))
        return false;
    if (config->bounce_slot_count == 0u ||
        config->bounce_slot_count > MD_RAW_BRIDGE_MAX_BOUNCE_SLOTS ||
        config->bounce_stride < MD_RAW_BRIDGE_MAX_TRANSFER ||
        (config->bounce_stride &
         (MD_RAW_BRIDGE_PAGE_SIZE - UINT32_C(1))) != 0u ||
        (config->bounce_base_pa &
         (MD_RAW_BRIDGE_PAGE_SIZE - UINT64_C(1))) != 0u)
        return false;
    bounce_length = (uint64_t)config->bounce_stride *
                    config->bounce_slot_count;
    return range_contains(config->ram_base, config->ram_size,
                          config->bounce_base_pa, bounce_length);
}

void md_raw_bridge_init(md_raw_bridge_t *bridge,
                        const md_raw_bridge_config_t *config) {
    md_raw_bridge_config_t saved_config = {0};

    if (bridge == NULL)
        return;
    if (config != NULL)
        saved_config = *config;
    memset(bridge, 0, sizeof *bridge);
    if (config != NULL)
        bridge->config = saved_config;
}

static arm_svc_result_t bridge_fail(md_raw_bridge_t *bridge,
                                    md_raw_bridge_error_t *error,
                                    md_raw_bridge_error_code_t code) {
    error->code = code;
    bridge->last_error = *error;
    increment_saturating_u64(&bridge->stats.failures);
    return ARM_SVC_ERROR;
}

static arm_svc_result_t redirect_to_target(arm_cpu_t *cpu, uint32_t target) {
    if ((target & 1u) != 0u) {
        cpu->cpsr |= ARM_CPSR_T;
        cpu->r[15] = target & ~UINT32_C(1);
    } else {
        cpu->cpsr &= ~ARM_CPSR_T;
        cpu->r[15] = target & ~UINT32_C(3);
    }
    return ARM_SVC_REDIRECTED;
}

static arm_svc_result_t guest_error(md_raw_bridge_t *bridge,
                                    arm_cpu_t *cpu,
                                    md_raw_bridge_error_t *error,
                                    md_raw_bridge_error_code_t code,
                                    int guest_errno,
                                    uint32_t return_lr) {
    error->code = code;
    error->guest_errno = guest_errno;
    bridge->last_error = *error;
    bridge->last_guest_error = *error;
    increment_saturating_u64(&bridge->stats.guest_errors);
    cpu->r[0] = (uint32_t)guest_errno;
    return redirect_to_target(cpu, return_lr);
}

static map_status_t collect_mapping(const md_raw_bridge_config_t *config,
                                    arm_cpu_t *cpu,
                                    uint32_t va,
                                    uint32_t length,
                                    arm_access_t access,
                                    bool privileged,
                                    mapped_span_t *spans,
                                    uint32_t span_capacity,
                                    uint32_t *span_count,
                                    md_raw_bridge_error_t *error) {
    uint64_t end = (uint64_t)va + length;
    uint32_t remaining = length;
    uint32_t current = va;

    if (end > MD_RAW_ADDRESS_SPACE_SIZE)
        return MAP_ADDRESS;

    while (remaining != 0u) {
        uint32_t permission_left = MD_RAW_BRIDGE_PERMISSION_GRANULE -
            (current & (MD_RAW_BRIDGE_PERMISSION_GRANULE - 1u));
        uint32_t chunk = remaining < permission_left
                             ? remaining : permission_left;
        uint32_t pa = 0u;
        uint32_t mmu_status = arm_mmu_translate(cpu, current, access,
                                                privileged, &pa);

        error->fault_va = current;
        error->fault_pa = pa;
        error->mmu_status = mmu_status;
        if (mmu_status != 0u)
            return MAP_TRANSLATION;
        if (!range_contains(config->ram_base, config->ram_size, pa, chunk))
            return MAP_RANGE;
        if (*span_count >= span_capacity)
            return MAP_CAPACITY;

        spans[*span_count].va = current;
        spans[*span_count].pa = pa;
        spans[*span_count].length = chunk;
        (*span_count)++;
        current += chunk;
        remaining -= chunk;
    }
    return MAP_OK;
}

static bool mapped_pa(const mapped_span_t *spans, uint32_t span_count,
                      uint32_t va, uint32_t length, uint32_t *pa) {
    uint32_t i;
    for (i = 0u; i < span_count; i++) {
        uint64_t offset;
        if (va < spans[i].va)
            continue;
        offset = (uint64_t)va - spans[i].va;
        if (offset <= spans[i].length &&
            length <= (uint64_t)spans[i].length - offset) {
            *pa = spans[i].pa + (uint32_t)offset;
            return true;
        }
    }
    return false;
}

static uint32_t mapped_load_u32(const md_raw_bridge_config_t *config,
                                const mapped_span_t *spans,
                                uint32_t span_count,
                                uint32_t va) {
    uint32_t pa = 0u;
    size_t offset;
    (void)mapped_pa(spans, span_count, va, 4u, &pa);
    offset = (size_t)((uint64_t)pa - config->ram_base);
    return load_u32(config->ram + offset);
}

static void mapped_store_u32(const md_raw_bridge_config_t *config,
                             uint32_t pa, uint32_t value) {
    size_t offset = (size_t)((uint64_t)pa - config->ram_base);
    store_u32(config->ram + offset, value);
}

static bool user_segment(uint32_t segment) {
    switch (segment) {
    case XNU32_UIO_USERSPACE:
    case XNU32_UIO_USERISPACE:
    case XNU32_UIO_PHYS_USERSPACE:
    case XNU32_UIO_USERSPACE32:
    case XNU32_UIO_USERISPACE32:
    case XNU32_UIO_PHYS_USERSPACE32:
    case XNU32_UIO_USERSPACE64:
    case XNU32_UIO_USERISPACE64:
    case XNU32_UIO_PHYS_USERSPACE64:
        return true;
    default:
        return false;
    }
}

static bool overlaps_metadata(uint32_t pa, uint32_t length,
                              const mapped_span_t *metadata,
                              uint32_t metadata_count) {
    uint32_t i;
    for (i = 0u; i < metadata_count; i++) {
        if (ranges_overlap(pa, length,
                           metadata[i].pa, metadata[i].length))
            return true;
    }
    return false;
}

static bool overlaps_bounce(const md_raw_bridge_config_t *config,
                            uint32_t pa, uint32_t length) {
    uint64_t reservation_length =
        (uint64_t)config->bounce_stride * config->bounce_slot_count;
    return ranges_overlap(pa, length, config->bounce_base_pa,
                          reservation_length);
}

static bool overlaps_pending_metadata(const md_raw_bridge_t *bridge,
                                      uint32_t pa, uint32_t length);

static map_status_t plan_user_data(md_raw_bridge_t *bridge,
                                   arm_cpu_t *cpu,
                                   const mapped_span_t *metadata,
                                   uint32_t metadata_count,
                                   arm_access_t access,
                                   md_raw_bridge_error_t *error) {
    const md_raw_bridge_config_t *config = &bridge->config;
    bool needs_redirect = false;
    uint32_t first_fault_va = 0u;
    uint32_t first_fault_pa = 0u;
    uint32_t first_mmu_status = 0u;
    uint32_t i;

    bridge->data_span_count = 0u;
    for (i = 0u; i < bridge->iov_plan_count; i++) {
        uint32_t va = bridge->iov_plan[i].base;
        uint32_t remaining = bridge->iov_plan[i].consumed;

        if ((uint64_t)va + remaining > MD_RAW_ADDRESS_SPACE_SIZE)
            return MAP_ADDRESS;

        while (remaining != 0u) {
            uint32_t permission_left = MD_RAW_BRIDGE_PERMISSION_GRANULE -
                (va & (MD_RAW_BRIDGE_PERMISSION_GRANULE - 1u));
            uint32_t chunk = remaining < permission_left
                                 ? remaining : permission_left;
            uint32_t pa = 0u;
            uint32_t mmu_status = arm_mmu_translate(cpu, va, access, false,
                                                    &pa);

            error->fault_va = va;
            error->fault_pa = pa;
            error->mmu_status = mmu_status;
            if (mmu_status != 0u) {
                if (!needs_redirect) {
                    first_fault_va = va;
                    first_fault_pa = pa;
                    first_mmu_status = mmu_status;
                }
                needs_redirect = true;
            } else {
                if (!range_contains(config->ram_base, config->ram_size,
                                    pa, chunk))
                    return MAP_RANGE;
                if (overlaps_metadata(pa, chunk, metadata, metadata_count))
                    return MAP_ALIAS;
                if (overlaps_pending_metadata(bridge, pa, chunk))
                    return MAP_PENDING_ALIAS;
                if (overlaps_bounce(config, pa, chunk))
                    return MAP_BOUNCE;
                if (bridge->data_span_count >=
                    MD_RAW_BRIDGE_MAX_DATA_SPANS)
                    return MAP_CAPACITY;

                bridge->data_spans[bridge->data_span_count].pa = pa;
                bridge->data_spans[bridge->data_span_count].length = chunk;
                bridge->data_span_count++;
            }
            va += chunk;
            remaining -= chunk;
        }
    }
    if (needs_redirect) {
        error->fault_va = first_fault_va;
        error->fault_pa = first_fault_pa;
        error->mmu_status = first_mmu_status;
        return MAP_TRANSLATION;
    }
    return MAP_OK;
}

static void copy_scratch_to_guest(md_raw_bridge_t *bridge) {
    uint32_t i;
    size_t scratch_offset = 0u;
    for (i = 0u; i < bridge->data_span_count; i++) {
        const md_raw_bridge_data_span_t *span = &bridge->data_spans[i];
        size_t ram_offset = (size_t)((uint64_t)span->pa -
                                     bridge->config.ram_base);
        memcpy(bridge->config.ram + ram_offset,
               bridge->scratch + scratch_offset, span->length);
        scratch_offset += span->length;
    }
}

static void copy_guest_to_scratch(md_raw_bridge_t *bridge) {
    uint32_t i;
    size_t scratch_offset = 0u;
    for (i = 0u; i < bridge->data_span_count; i++) {
        const md_raw_bridge_data_span_t *span = &bridge->data_spans[i];
        size_t ram_offset = (size_t)((uint64_t)span->pa -
                                     bridge->config.ram_base);
        memcpy(bridge->scratch + scratch_offset,
               bridge->config.ram + ram_offset, span->length);
        scratch_offset += span->length;
    }
}

static uint64_t guarded_media_end(const md_raw_bridge_config_t *config) {
    uint64_t maximum = (uint64_t)INT64_MAX;
    if (config->media_size > maximum - MD_RAW_BRIDGE_MAX_TRANSFER)
        return maximum;
    return config->media_size + MD_RAW_BRIDGE_MAX_TRANSFER;
}

static uint32_t media_prefix_length(const md_raw_bridge_config_t *config,
                                    uint64_t offset, uint32_t requested) {
    uint64_t available;
    if (offset >= config->media_size)
        return 0u;
    available = config->media_size - offset;
    return available < requested ? (uint32_t)available : requested;
}

static uint32_t guard_tail_offset(const md_raw_bridge_config_t *config,
                                  uint64_t offset) {
    if (offset <= config->media_size)
        return 0u;
    return (uint32_t)(offset - config->media_size);
}

static uint32_t physical_user_segment(uint32_t segment) {
    /* Exact 7E18 _mdevrw mapping at c0073fc8. */
    if (segment == XNU32_UIO_USERSPACE64)
        return XNU32_UIO_PHYS_USERSPACE64;
    if (segment == XNU32_UIO_USERSPACE32)
        return XNU32_UIO_PHYS_USERSPACE32;
    return XNU32_UIO_PHYS_USERSPACE;
}

static md_raw_bridge_pending_t *find_pending(md_raw_bridge_t *bridge,
                                             uint32_t sp,
                                             uint32_t mode) {
    uint32_t i;
    for (i = 0u; i < bridge->config.bounce_slot_count; i++) {
        if (bridge->pending[i].active &&
            bridge->pending[i].key_sp == sp &&
            bridge->pending[i].key_mode == mode)
            return &bridge->pending[i];
    }
    return NULL;
}

static bool pending_sp_active(const md_raw_bridge_t *bridge, uint32_t sp) {
    uint32_t i;
    for (i = 0u; i < bridge->config.bounce_slot_count; i++) {
        if (bridge->pending[i].active &&
            bridge->pending[i].key_sp == sp)
            return true;
    }
    return false;
}

static bool pending_metadata_alias(const md_raw_bridge_t *bridge,
                                   const mapped_span_t *spans,
                                   uint32_t span_count) {
    uint32_t pending_index;
    uint32_t span_index;

    for (pending_index = 0u;
         pending_index < bridge->config.bounce_slot_count;
         pending_index++) {
        const md_raw_bridge_pending_t *pending =
            &bridge->pending[pending_index];
        uint32_t pending_span;
        if (!pending->active)
            continue;
        for (span_index = 0u; span_index < span_count; span_index++) {
            for (pending_span = 0u;
                 pending_span < pending->metadata_span_count;
                 pending_span++) {
                if (ranges_overlap(
                        spans[span_index].pa, spans[span_index].length,
                        pending->metadata_spans[pending_span].pa,
                        pending->metadata_spans[pending_span].length))
                    return true;
            }
        }
    }
    return false;
}

static bool overlaps_pending_metadata(const md_raw_bridge_t *bridge,
                                      uint32_t pa, uint32_t length) {
    mapped_span_t span;
    span.va = 0u;
    span.pa = pa;
    span.length = length;
    return pending_metadata_alias(bridge, &span, 1u);
}

static md_raw_bridge_pending_t *allocate_pending(md_raw_bridge_t *bridge,
                                                 uint32_t sp,
                                                 bool *collision) {
    md_raw_bridge_pending_t *available = NULL;
    uint32_t i;

    *collision = false;
    for (i = 0u; i < bridge->config.bounce_slot_count; i++) {
        md_raw_bridge_pending_t *pending = &bridge->pending[i];
        if (pending->active) {
            if (pending->key_sp == sp) {
                *collision = true;
                return NULL;
            }
        } else if (available == NULL) {
            available = pending;
        }
    }
    return available;
}

static uint8_t *pending_bounce(md_raw_bridge_t *bridge,
                               const md_raw_bridge_pending_t *pending) {
    size_t offset = (size_t)(pending->bounce_pa - bridge->config.ram_base);
    return bridge->config.ram + offset;
}

static void account_transfer(md_raw_bridge_t *bridge, uint32_t rw,
                             uint32_t done, uint32_t media_done) {
    uint32_t guard_done = done - media_done;

    if (rw == XNU32_UIO_READ) {
        bridge->stats.bytes_read =
            add_saturating_u64(bridge->stats.bytes_read, done);
        bridge->stats.media_bytes_read =
            add_saturating_u64(bridge->stats.media_bytes_read, media_done);
        bridge->stats.guard_bytes_read =
            add_saturating_u64(bridge->stats.guard_bytes_read, guard_done);
    } else {
        bridge->stats.bytes_written =
            add_saturating_u64(bridge->stats.bytes_written, done);
        bridge->stats.media_bytes_written =
            add_saturating_u64(bridge->stats.media_bytes_written, media_done);
        bridge->stats.guard_bytes_written =
            add_saturating_u64(bridge->stats.guard_bytes_written, guard_done);
    }
}

static void restore_pending_segment(md_raw_bridge_t *bridge,
                                    const md_raw_bridge_pending_t *pending) {
    mapped_store_u32(&bridge->config, pending->uio_segment_pa,
                     pending->original_segment);
}

static arm_svc_result_t handle_completion(md_raw_bridge_t *bridge,
                                          arm_cpu_t *cpu,
                                          uint32_t pc,
                                          uint32_t encoding) {
    const md_raw_bridge_config_t *config = &bridge->config;
    md_raw_bridge_error_t error = {0};
    mapped_span_t metadata[MD_RAW_BRIDGE_MAX_METADATA_SPANS];
    uint32_t metadata_count = 0u;
    md_raw_bridge_pending_t *pending;
    uint32_t iovs_pa = 0u;
    uint32_t iovcnt_pa = 0u;
    uint32_t segment_pa = 0u;
    uint32_t rw_pa = 0u;
    uint32_t resid_pa = 0u;
    uint32_t offset_lo_pa = 0u;
    uint32_t offset_hi_pa = 0u;
    int32_t current_residual;
    uint64_t current_offset;
    uint64_t expected_offset;
    uint32_t done;
    uint32_t media_done;
    uint32_t guard_done;
    uint32_t guard_offset;
    uint32_t return_lr;
    uint32_t guest_errno;
    uint32_t i;
    map_status_t map_status;
    size_t transferred = 0u;
    vm_block_status_t block_status;

    error.pc = pc;
    error.encoding = encoding;
    error.device = config->expected_device;
    pending = find_pending(bridge, cpu->r[13],
                           cpu->cpsr & ARM_CPSR_MODE_MASK);
    if (pending == NULL)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_STALE_COMPLETION);

    error.uio_va = pending->uio_va;
    error.segment = pending->original_segment;
    error.rw = pending->rw;
    error.direction = pending->rw == XNU32_UIO_READ
                          ? MD_RAW_BRIDGE_DIRECTION_READ
                          : MD_RAW_BRIDGE_DIRECTION_WRITE;
    error.residual = pending->original_residual;
    error.media_offset = pending->original_offset;

    map_status = collect_mapping(config, cpu, pending->uio_va,
                                 MD_RAW_BRIDGE_UIO_SIZE,
                                 ARM_ACCESS_WRITE, true,
                                 metadata,
                                 MD_RAW_BRIDGE_MAX_METADATA_SPANS,
                                 &metadata_count, &error);
    if (map_status != MAP_OK ||
        metadata_count != pending->uio_span_count) {
        memset(pending, 0, sizeof *pending);
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_MALFORMED_COMPLETION);
    }
    for (i = 0u; i < metadata_count; i++) {
        if (metadata[i].pa != pending->metadata_spans[i].pa ||
            metadata[i].length != pending->metadata_spans[i].length) {
            memset(pending, 0, sizeof *pending);
            return bridge_fail(bridge, &error,
                               MD_RAW_BRIDGE_ERROR_MALFORMED_COMPLETION);
        }
    }
    if (!mapped_pa(metadata, metadata_count,
                   pending->uio_va + XNU32_UIO_IOVS, 4u, &iovs_pa) ||
        !mapped_pa(metadata, metadata_count,
                   pending->uio_va + XNU32_UIO_IOVCNT, 4u, &iovcnt_pa) ||
        !mapped_pa(metadata, metadata_count,
                   pending->uio_va + XNU32_UIO_SEGFLG, 4u, &segment_pa) ||
        !mapped_pa(metadata, metadata_count,
                   pending->uio_va + XNU32_UIO_RW, 4u, &rw_pa) ||
        !mapped_pa(metadata, metadata_count,
                   pending->uio_va + XNU32_UIO_RESID, 4u, &resid_pa) ||
        !mapped_pa(metadata, metadata_count,
                   pending->uio_va + XNU32_UIO_OFFSET_LO, 4u,
                   &offset_lo_pa) ||
        !mapped_pa(metadata, metadata_count,
                   pending->uio_va + XNU32_UIO_OFFSET_HI, 4u,
                   &offset_hi_pa) ||
        iovs_pa != pending->uio_iovs_pa ||
        iovcnt_pa != pending->uio_iovcnt_pa ||
        segment_pa != pending->uio_segment_pa ||
        rw_pa != pending->uio_rw_pa ||
        resid_pa != pending->uio_resid_pa ||
        offset_lo_pa != pending->uio_offset_lo_pa ||
        offset_hi_pa != pending->uio_offset_hi_pa) {
        memset(pending, 0, sizeof *pending);
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_MALFORMED_COMPLETION);
    }

    current_residual = (int32_t)mapped_load_u32(
        config, metadata, metadata_count,
        pending->uio_va + XNU32_UIO_RESID);
    current_offset = mapped_load_u32(
        config, metadata, metadata_count,
        pending->uio_va + XNU32_UIO_OFFSET_LO);
    current_offset |= (uint64_t)mapped_load_u32(
        config, metadata, metadata_count,
        pending->uio_va + XNU32_UIO_OFFSET_HI) << 32;
    guest_errno = cpu->r[0];

    /*
     * Do not validate the live LR here. Thumb uiomove returns through a saved
     * PC and may leave r14 clobbered by its final nested call. The exact E4
     * site plus the active SP-keyed continuation identify this completion.
     */
    if ((guest_errno != 0u &&
         guest_errno != (uint32_t)XNU_ERR_EFAULT) ||
        mapped_load_u32(config, metadata, metadata_count,
                        pending->uio_va + XNU32_UIO_SEGFLG) !=
            pending->physical_segment ||
        mapped_load_u32(config, metadata, metadata_count,
                        pending->uio_va + XNU32_UIO_RW) != pending->rw ||
        current_residual < 0 ||
        current_residual > pending->original_residual) {
        restore_pending_segment(bridge, pending);
        memset(pending, 0, sizeof *pending);
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_MALFORMED_COMPLETION);
    }

    done = (uint32_t)(pending->original_residual - current_residual);
    expected_offset = pending->original_offset + done;
    if (current_offset != expected_offset ||
        (guest_errno == 0u &&
         done != (uint32_t)pending->original_residual) ||
        (guest_errno != 0u &&
         done == (uint32_t)pending->original_residual)) {
        restore_pending_segment(bridge, pending);
        memset(pending, 0, sizeof *pending);
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_MALFORMED_COMPLETION);
    }

    /*
     * Native uiomove already committed its uio/iovec progress. Restore the
     * caller's segment before any host operation; unlike CPU state, these
     * guest-memory writes cannot be rolled back if the backend subsequently
     * fails.
     */
    restore_pending_segment(bridge, pending);
    media_done = done < pending->media_length
                     ? done : pending->media_length;
    guard_done = done - media_done;
    guard_offset = guard_tail_offset(config, pending->original_offset);
    if (pending->rw == XNU32_UIO_WRITE && media_done != 0u) {
        block_status = vm_block_write_exact(
            config->block, pending->original_offset,
            pending_bounce(bridge, pending), media_done,
            config->cancelled, config->cancel_context, &transferred);
        error.block_status = block_status;
        error.transferred = transferred;
        if (block_status != VM_BLOCK_STATUS_OK) {
            memset(pending, 0, sizeof *pending);
            return bridge_fail(bridge, &error,
                               MD_RAW_BRIDGE_ERROR_BLOCK_IO);
        }
    }
    if (pending->rw == XNU32_UIO_WRITE && guard_done != 0u) {
        memcpy(bridge->guard_tail + guard_offset,
               pending_bounce(bridge, pending) + media_done,
               guard_done);
    }

    return_lr = pending->return_lr;
    account_transfer(bridge, pending->rw, done, media_done);
    increment_saturating_u64(&bridge->stats.redirected_completions);
    if (guest_errno == 0u) {
        if (pending->rw == XNU32_UIO_READ)
            increment_saturating_u64(&bridge->stats.successful_reads);
        else
            increment_saturating_u64(&bridge->stats.successful_writes);
    } else {
        error.code = MD_RAW_BRIDGE_ERROR_UIOMOVE;
        error.guest_errno = (int)guest_errno;
        error.residual = current_residual;
        error.transferred = done;
        bridge->last_error = error;
        bridge->last_guest_error = error;
        increment_saturating_u64(&bridge->stats.guest_errors);
        if (done != 0u)
            increment_saturating_u64(
                &bridge->stats.partial_uiomove_errors);
    }
    memset(pending, 0, sizeof *pending);
    cpu->r[0] = guest_errno;
    cpu->r[14] = return_lr;
    return redirect_to_target(cpu, return_lr);
}

static arm_svc_result_t start_uiomove(md_raw_bridge_t *bridge,
                                      arm_cpu_t *cpu,
                                      md_raw_bridge_error_t *error,
                                      uint32_t uio_va,
                                      const mapped_span_t *metadata,
                                      uint32_t uio_span_count,
                                      uint32_t metadata_count,
                                      uint32_t segment,
                                      uint32_t rw,
                                      int32_t residual,
                                      uint64_t offset,
                                      uint32_t media_length,
                                      uint32_t return_lr) {
    const md_raw_bridge_config_t *config = &bridge->config;
    md_raw_bridge_pending_t *pending;
    uint8_t *bounce;
    bool collision;
    size_t transferred = 0u;
    vm_block_status_t block_status;
    uint32_t slot_index;
    uint32_t i;
    uint32_t guard_length;
    uint32_t guard_offset;

    pending = allocate_pending(bridge, cpu->r[13], &collision);
    if (pending == NULL) {
        return guest_error(bridge, cpu, error,
                           collision
                               ? MD_RAW_BRIDGE_ERROR_PENDING_COLLISION
                               : MD_RAW_BRIDGE_ERROR_PENDING_EXHAUSTED,
                           XNU_ERR_EBUSY, return_lr);
    }

    slot_index = (uint32_t)(pending - bridge->pending);
    memset(pending, 0, sizeof *pending);
    if (uio_span_count == 0u || uio_span_count > metadata_count ||
        metadata_count > MD_RAW_BRIDGE_MAX_METADATA_SPANS)
        return bridge_fail(bridge, error,
                           MD_RAW_BRIDGE_ERROR_UIO_RANGE);
    pending->active = true;
    pending->key_sp = cpu->r[13];
    pending->key_mode = cpu->cpsr & ARM_CPSR_MODE_MASK;
    pending->return_lr = return_lr;
    pending->uio_va = uio_va;
    pending->uio_span_count = uio_span_count;
    pending->metadata_span_count = metadata_count;
    for (i = 0u; i < metadata_count; i++) {
        pending->metadata_spans[i].pa = metadata[i].pa;
        pending->metadata_spans[i].length = metadata[i].length;
    }
    if (!mapped_pa(metadata, uio_span_count,
                   uio_va + XNU32_UIO_IOVS, 4u,
                   &pending->uio_iovs_pa) ||
        !mapped_pa(metadata, uio_span_count,
                   uio_va + XNU32_UIO_IOVCNT, 4u,
                   &pending->uio_iovcnt_pa) ||
        !mapped_pa(metadata, uio_span_count,
                   uio_va + XNU32_UIO_OFFSET_LO, 4u,
                   &pending->uio_offset_lo_pa) ||
        !mapped_pa(metadata, uio_span_count,
                   uio_va + XNU32_UIO_OFFSET_HI, 4u,
                   &pending->uio_offset_hi_pa) ||
        !mapped_pa(metadata, uio_span_count,
                   uio_va + XNU32_UIO_SEGFLG, 4u,
                   &pending->uio_segment_pa) ||
        !mapped_pa(metadata, uio_span_count,
                   uio_va + XNU32_UIO_RW, 4u,
                   &pending->uio_rw_pa) ||
        !mapped_pa(metadata, uio_span_count,
                   uio_va + XNU32_UIO_RESID, 4u,
                   &pending->uio_resid_pa)) {
        memset(pending, 0, sizeof *pending);
        return bridge_fail(bridge, error,
                           MD_RAW_BRIDGE_ERROR_UIO_RANGE);
    }
    pending->original_segment = segment;
    pending->physical_segment = physical_user_segment(segment);
    pending->rw = rw;
    pending->original_residual = residual;
    pending->original_offset = offset;
    pending->bounce_pa =
        config->bounce_base_pa + (uint64_t)slot_index *
                                     config->bounce_stride;
    pending->media_length = media_length;
    bounce = pending_bounce(bridge, pending);

    memset(bounce, 0, (size_t)residual);
    if (rw == XNU32_UIO_READ && media_length != 0u) {
        block_status = vm_block_read_exact(
            config->block, offset, bounce, media_length,
            config->cancelled, config->cancel_context, &transferred);
        error->block_status = block_status;
        error->transferred = transferred;
        if (block_status != VM_BLOCK_STATUS_OK) {
            memset(pending, 0, sizeof *pending);
            return bridge_fail(bridge, error,
                               MD_RAW_BRIDGE_ERROR_BLOCK_IO);
        }
    }
    guard_length = (uint32_t)residual - media_length;
    guard_offset = guard_tail_offset(config, offset);
    if (rw == XNU32_UIO_READ && guard_length != 0u) {
        memcpy(bounce + media_length,
               bridge->guard_tail + guard_offset, guard_length);
    }

    mapped_store_u32(config, pending->uio_segment_pa,
                     pending->physical_segment);
    cpu->r[0] = (uint32_t)pending->bounce_pa;
    cpu->r[1] = (uint32_t)(pending->bounce_pa >> 32);
    cpu->r[2] = (uint32_t)residual;
    cpu->r[3] = uio_va;
    cpu->r[14] = config->completion_site.pc | UINT32_C(1);
    increment_saturating_u64(&bridge->stats.redirected_requests);
    return redirect_to_target(cpu, config->uiomove_thumb_pc | UINT32_C(1));
}

arm_svc_result_t md_raw_bridge_handle_svc(void *context, arm_cpu_t *cpu,
                                          uint32_t pc, uint32_t encoding) {
    md_raw_bridge_t *bridge = (md_raw_bridge_t *)context;
    const md_raw_bridge_config_t *config;
    md_raw_bridge_error_t error = {0};
    mapped_span_t metadata[MD_RAW_BRIDGE_MAX_METADATA_SPANS];
    uint32_t metadata_count = 0u;
    uint32_t uio_span_count = 0u;
    uint32_t mode;
    uint32_t uio_va;
    uint32_t iov_va;
    int32_t iov_count;
    uint64_t offset;
    uint64_t aperture_end;
    uint32_t segment;
    uint32_t rw;
    int32_t residual;
    int32_t max_iovs;
    uint32_t flags;
    uint64_t iov_sum = 0u;
    uint32_t transfer_length;
    uint32_t media_length;
    uint32_t guard_length;
    uint32_t guard_offset;
    uint32_t return_lr;
    uint32_t residual_to_plan;
    uint32_t i;
    uint32_t uio_iovs_pa = 0u;
    uint32_t uio_iovcnt_pa = 0u;
    uint32_t uio_offset_lo_pa = 0u;
    uint32_t uio_offset_hi_pa = 0u;
    uint32_t uio_resid_pa = 0u;
    uint32_t uio_segment_pa = 0u;
    map_status_t map_status;
    size_t transferred = 0u;
    vm_block_status_t block_status;

    if (bridge == NULL)
        return ARM_SVC_ERROR;
    config = &bridge->config;
    if ((pc != config->site.pc || encoding != config->site.encoding) &&
        (pc != config->completion_site.pc ||
         encoding != config->completion_site.encoding))
        return ARM_SVC_UNHANDLED;

    error.pc = pc;
    error.encoding = encoding;
    bridge->iov_plan_count = 0u;
    bridge->data_span_count = 0u;

    if (cpu == NULL)
        return bridge_fail(bridge, &error, MD_RAW_BRIDGE_ERROR_NULL_CPU);
    if ((cpu->cpsr & ARM_CPSR_T) == 0u)
        return ARM_SVC_UNHANDLED;
    mode = cpu->cpsr & ARM_CPSR_MODE_MASK;
    if (mode == ARM_MODE_USR)
        return ARM_SVC_UNHANDLED;
    if (!arm_mode_is_valid(mode))
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_INVALID_MODE);
    if (!md_raw_bridge_config_valid(config))
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_INVALID_CONFIG);
    if ((cpu->cp15.sctlr & ARM_SCTLR_M) == 0u)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_MMU_DISABLED);
    if (cpu->bus == NULL || cpu->bus->read32 == NULL)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_MISSING_BUS_ACCESS);
    if (pc == config->completion_site.pc)
        return handle_completion(bridge, cpu, pc, encoding);

    return_lr = cpu->r[14];
    if (pending_sp_active(bridge, cpu->r[13]))
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_PENDING_COLLISION,
                           XNU_ERR_EBUSY, return_lr);
    error.device = cpu->r[0];
    if (error.device != config->expected_device)
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_DEVICE, XNU_ERR_ENXIO,
                           return_lr);

    uio_va = cpu->r[1];
    error.uio_va = uio_va;
    if ((uio_va & 3u) != 0u)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_UIO_ALIGNMENT);

    map_status = collect_mapping(config, cpu, uio_va,
                                 MD_RAW_BRIDGE_UIO_SIZE,
                                 ARM_ACCESS_WRITE, true,
                                 metadata,
                                 MD_RAW_BRIDGE_MAX_METADATA_SPANS,
                                 &metadata_count, &error);
    if (map_status == MAP_TRANSLATION)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_UIO_TRANSLATION);
    if (map_status != MAP_OK)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_UIO_RANGE);
    uio_span_count = metadata_count;
    for (i = 0u; i < metadata_count; i++) {
        if (overlaps_bounce(config, metadata[i].pa, metadata[i].length))
            return bridge_fail(bridge, &error,
                               MD_RAW_BRIDGE_ERROR_BOUNCE_ALIAS);
    }
    if (!mapped_pa(metadata, metadata_count,
                   uio_va + XNU32_UIO_IOVS, 4u, &uio_iovs_pa) ||
        !mapped_pa(metadata, metadata_count,
                   uio_va + XNU32_UIO_IOVCNT, 4u, &uio_iovcnt_pa) ||
        !mapped_pa(metadata, metadata_count,
                   uio_va + XNU32_UIO_OFFSET_LO, 4u, &uio_offset_lo_pa) ||
        !mapped_pa(metadata, metadata_count,
                   uio_va + XNU32_UIO_OFFSET_HI, 4u, &uio_offset_hi_pa) ||
        !mapped_pa(metadata, metadata_count,
                   uio_va + XNU32_UIO_SEGFLG, 4u, &uio_segment_pa) ||
        !mapped_pa(metadata, metadata_count,
                   uio_va + XNU32_UIO_RESID, 4u, &uio_resid_pa))
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_UIO_RANGE);
    iov_va = mapped_load_u32(config, metadata, metadata_count,
                             uio_va + XNU32_UIO_IOVS);
    iov_count = (int32_t)mapped_load_u32(config, metadata, metadata_count,
                                         uio_va + XNU32_UIO_IOVCNT);
    offset = mapped_load_u32(config, metadata, metadata_count,
                             uio_va + XNU32_UIO_OFFSET_LO);
    offset |= (uint64_t)mapped_load_u32(config, metadata, metadata_count,
                                        uio_va + XNU32_UIO_OFFSET_HI) << 32;
    segment = mapped_load_u32(config, metadata, metadata_count,
                              uio_va + XNU32_UIO_SEGFLG);
    rw = mapped_load_u32(config, metadata, metadata_count,
                         uio_va + XNU32_UIO_RW);
    residual = (int32_t)mapped_load_u32(config, metadata, metadata_count,
                                        uio_va + XNU32_UIO_RESID);
    max_iovs = (int32_t)mapped_load_u32(config, metadata, metadata_count,
                                        uio_va + XNU32_UIO_MAX_IOVS);
    flags = mapped_load_u32(config, metadata, metadata_count,
                            uio_va + XNU32_UIO_FLAGS);

    error.iov_va = iov_va;
    error.iov_count = iov_count;
    error.media_offset = offset;
    error.segment = segment;
    error.rw = rw;
    error.residual = residual;
    error.fault_va = 0u;
    error.fault_pa = 0u;
    error.mmu_status = 0u;

    if (!user_segment(segment))
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_SEGMENT);
    if (rw == XNU32_UIO_READ)
        error.direction = MD_RAW_BRIDGE_DIRECTION_READ;
    else if (rw == XNU32_UIO_WRITE)
        error.direction = MD_RAW_BRIDGE_DIRECTION_WRITE;
    else
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_DIRECTION);

    if ((flags & XNU32_UIO_FLAGS_INITED) == 0u || max_iovs < 0 ||
        iov_count > max_iovs)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_UIO_STATE);
    if (residual == 0) {
        cpu->r[0] = 0u;
        increment_saturating_u64(&bridge->stats.zero_length_requests);
        return redirect_to_target(cpu, return_lr);
    }
    if (residual < 0)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_RESIDUAL);
    if ((offset & (UINT64_C(1) << 63)) != 0u)
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_OFFSET, XNU_ERR_EINVAL,
                           return_lr);
    if ((uint32_t)residual > MD_RAW_BRIDGE_MAX_TRANSFER)
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_RESIDUAL, XNU_ERR_EINVAL,
                           return_lr);
    aperture_end = guarded_media_end(config);
    if (offset > aperture_end ||
        (uint32_t)residual > aperture_end - offset)
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_MEDIA_RANGE,
                           XNU_ERR_EINVAL, return_lr);
    transfer_length = (uint32_t)residual;
    media_length = media_prefix_length(config, offset, transfer_length);
    guard_length = transfer_length - media_length;
    guard_offset = guard_tail_offset(config, offset);
    if (iov_count <= 0 || (uint32_t)iov_count > MD_RAW_BRIDGE_MAX_IOVECS)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_IOV_COUNT);
    if ((iov_va & 3u) != 0u)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_IOV_ALIGNMENT);

    map_status = collect_mapping(
        config, cpu, iov_va,
        (uint32_t)iov_count * MD_RAW_BRIDGE_USER_IOV_SIZE,
        ARM_ACCESS_WRITE, true,
        metadata, MD_RAW_BRIDGE_MAX_METADATA_SPANS,
        &metadata_count, &error);
    if (map_status == MAP_TRANSLATION)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_IOV_TRANSLATION);
    if (map_status != MAP_OK)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_IOV_RANGE);

    for (i = 1u; i < metadata_count; i++) {
        uint32_t j;
        for (j = 0u; j < i; j++) {
            if (ranges_overlap(metadata[i].pa, metadata[i].length,
                               metadata[j].pa, metadata[j].length))
                return bridge_fail(bridge, &error,
                                   MD_RAW_BRIDGE_ERROR_METADATA_ALIAS);
        }
    }
    for (i = 0u; i < metadata_count; i++) {
        if (overlaps_bounce(config, metadata[i].pa, metadata[i].length))
            return bridge_fail(bridge, &error,
                               MD_RAW_BRIDGE_ERROR_BOUNCE_ALIAS);
    }
    if (pending_metadata_alias(bridge, metadata, metadata_count))
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_PENDING_COLLISION,
                           XNU_ERR_EBUSY, return_lr);

    error.fault_va = 0u;
    error.fault_pa = 0u;
    error.mmu_status = 0u;

    bridge->iov_plan_count = (uint32_t)iov_count;
    residual_to_plan = transfer_length;
    for (i = 0u; i < bridge->iov_plan_count; i++) {
        uint32_t entry_va = iov_va + i * MD_RAW_BRIDGE_USER_IOV_SIZE;
        md_raw_bridge_iov_plan_t *plan = &bridge->iov_plan[i];

        if (!mapped_pa(metadata, metadata_count, entry_va, 4u,
                       &plan->base_pa) ||
            !mapped_pa(metadata, metadata_count, entry_va + 4u, 4u,
                       &plan->length_pa))
            return bridge_fail(bridge, &error,
                               MD_RAW_BRIDGE_ERROR_IOV_RANGE);
        plan->base = mapped_load_u32(config, metadata, metadata_count,
                                     entry_va);
        plan->length = mapped_load_u32(config, metadata, metadata_count,
                                       entry_va + 4u);
        plan->consumed = plan->length < residual_to_plan
                             ? plan->length : residual_to_plan;
        residual_to_plan -= plan->consumed;
        if (plan->consumed != 0u &&
            (plan->base >= config->user_address_limit ||
             plan->consumed > config->user_address_limit - plan->base)) {
            error.fault_va = plan->base;
            return guest_error(bridge, cpu, &error,
                               MD_RAW_BRIDGE_ERROR_USER_ADDRESS,
                               XNU_ERR_EFAULT, return_lr);
        }
        iov_sum += plan->length;
    }
    if (iov_sum < (uint32_t)residual || residual_to_plan != 0u)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_IOV_SUM);

    map_status = plan_user_data(
        bridge, cpu, metadata, metadata_count,
        rw == XNU32_UIO_READ ? ARM_ACCESS_WRITE : ARM_ACCESS_READ,
        &error);
    if (map_status == MAP_TRANSLATION)
        return start_uiomove(bridge, cpu, &error, uio_va,
                             metadata, uio_span_count, metadata_count,
                             segment, rw, residual, offset, media_length,
                             return_lr);
    if (map_status == MAP_RANGE)
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_USER_RANGE,
                           XNU_ERR_EFAULT, return_lr);
    if (map_status == MAP_ALIAS)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_METADATA_ALIAS);
    if (map_status == MAP_PENDING_ALIAS)
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_PENDING_COLLISION,
                           XNU_ERR_EBUSY, return_lr);
    if (map_status == MAP_BOUNCE)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_BOUNCE_ALIAS);
    if (map_status == MAP_CAPACITY)
        return bridge_fail(bridge, &error,
                           MD_RAW_BRIDGE_ERROR_PLAN_CAPACITY);
    if (map_status != MAP_OK)
        return guest_error(bridge, cpu, &error,
                           MD_RAW_BRIDGE_ERROR_USER_ADDRESS,
                           XNU_ERR_EFAULT, return_lr);

    error.fault_va = 0u;
    error.fault_pa = 0u;
    error.mmu_status = 0u;

    if (rw == XNU32_UIO_READ) {
        if (media_length != 0u) {
            block_status = vm_block_read_exact(
                config->block, offset, bridge->scratch, (size_t)media_length,
                config->cancelled, config->cancel_context, &transferred);
            error.block_status = block_status;
            error.transferred = transferred;
            if (block_status != VM_BLOCK_STATUS_OK)
                return bridge_fail(bridge, &error,
                                   MD_RAW_BRIDGE_ERROR_BLOCK_IO);
        }
        if (guard_length != 0u) {
            memcpy(bridge->scratch + media_length,
                   bridge->guard_tail + guard_offset, guard_length);
        }
        copy_scratch_to_guest(bridge);
    } else {
        copy_guest_to_scratch(bridge);
        if (media_length != 0u) {
            block_status = vm_block_write_exact(
                config->block, offset, bridge->scratch, (size_t)media_length,
                config->cancelled, config->cancel_context, &transferred);
            error.block_status = block_status;
            error.transferred = transferred;
            if (block_status != VM_BLOCK_STATUS_OK)
                return bridge_fail(bridge, &error,
                                   MD_RAW_BRIDGE_ERROR_BLOCK_IO);
        }
        if (guard_length != 0u) {
            memcpy(bridge->guard_tail + guard_offset,
                   bridge->scratch + media_length, guard_length);
        }
    }

    /* Every metadata address below was pretranslated for privileged writes;
     * guest execution cannot change the mappings during this handler. */
    for (i = 0u; i < bridge->iov_plan_count; i++) {
        md_raw_bridge_iov_plan_t *plan = &bridge->iov_plan[i];
        if (plan->consumed != 0u) {
            mapped_store_u32(config, plan->base_pa,
                             plan->base + plan->consumed);
            mapped_store_u32(config, plan->length_pa,
                             plan->length - plan->consumed);
        }
    }
    offset += transfer_length;
    {
        uint32_t current_index = (uint32_t)iov_count - 1u;
        uint32_t current_count = 0u;
        for (i = 0u; i < bridge->iov_plan_count; i++) {
            const md_raw_bridge_iov_plan_t *plan = &bridge->iov_plan[i];
            if (plan->length - plan->consumed != 0u) {
                current_index = i;
                current_count = bridge->iov_plan_count - i;
                break;
            }
        }
        mapped_store_u32(config, uio_iovs_pa,
                         iov_va + current_index *
                                  MD_RAW_BRIDGE_USER_IOV_SIZE);
        mapped_store_u32(config, uio_iovcnt_pa, current_count);
    }
    mapped_store_u32(config, uio_offset_lo_pa, (uint32_t)offset);
    mapped_store_u32(config, uio_offset_hi_pa, (uint32_t)(offset >> 32));
    mapped_store_u32(config, uio_resid_pa,
                     (uint32_t)residual - transfer_length);

    cpu->r[0] = 0u;
    if (rw == XNU32_UIO_READ) {
        increment_saturating_u64(&bridge->stats.successful_reads);
    } else {
        increment_saturating_u64(&bridge->stats.successful_writes);
    }
    account_transfer(bridge, rw, transfer_length, media_length);
    return redirect_to_target(cpu, return_lr);
}

const char *md_raw_bridge_error_string(md_raw_bridge_error_code_t code) {
    switch (code) {
    case MD_RAW_BRIDGE_ERROR_NONE: return "none";
    case MD_RAW_BRIDGE_ERROR_NULL_CPU: return "null CPU";
    case MD_RAW_BRIDGE_ERROR_INVALID_MODE: return "invalid CPU mode";
    case MD_RAW_BRIDGE_ERROR_INVALID_CONFIG: return "invalid configuration";
    case MD_RAW_BRIDGE_ERROR_MMU_DISABLED: return "MMU disabled";
    case MD_RAW_BRIDGE_ERROR_MISSING_BUS_ACCESS:
        return "missing MMU page-table access";
    case MD_RAW_BRIDGE_ERROR_DEVICE: return "unsupported raw device";
    case MD_RAW_BRIDGE_ERROR_UIO_ALIGNMENT: return "unaligned uio";
    case MD_RAW_BRIDGE_ERROR_UIO_TRANSLATION: return "uio translation fault";
    case MD_RAW_BRIDGE_ERROR_UIO_RANGE: return "uio outside guest RAM";
    case MD_RAW_BRIDGE_ERROR_UIO_STATE: return "invalid uio state";
    case MD_RAW_BRIDGE_ERROR_SEGMENT: return "unsupported uio segment";
    case MD_RAW_BRIDGE_ERROR_DIRECTION: return "invalid uio direction";
    case MD_RAW_BRIDGE_ERROR_RESIDUAL: return "invalid or excessive residual";
    case MD_RAW_BRIDGE_ERROR_OFFSET: return "negative media offset";
    case MD_RAW_BRIDGE_ERROR_MEDIA_RANGE:
        return "raw request outside bounded media guard aperture";
    case MD_RAW_BRIDGE_ERROR_IOV_COUNT: return "invalid iovec count";
    case MD_RAW_BRIDGE_ERROR_IOV_ALIGNMENT: return "unaligned iovec array";
    case MD_RAW_BRIDGE_ERROR_IOV_TRANSLATION:
        return "iovec translation fault";
    case MD_RAW_BRIDGE_ERROR_IOV_RANGE: return "iovec outside guest RAM";
    case MD_RAW_BRIDGE_ERROR_IOV_SUM: return "iovec/residual mismatch";
    case MD_RAW_BRIDGE_ERROR_USER_ADDRESS:
        return "user address outside configured user VA range";
    case MD_RAW_BRIDGE_ERROR_USER_TRANSLATION:
        return "user buffer translation fault";
    case MD_RAW_BRIDGE_ERROR_USER_RANGE:
        return "user buffer outside guest RAM";
    case MD_RAW_BRIDGE_ERROR_METADATA_ALIAS:
        return "user data aliases uio metadata";
    case MD_RAW_BRIDGE_ERROR_BOUNCE_ALIAS:
        return "guest request aliases reserved raw bounce RAM";
    case MD_RAW_BRIDGE_ERROR_PLAN_CAPACITY:
        return "bounded transfer plan exhausted";
    case MD_RAW_BRIDGE_ERROR_PENDING_COLLISION:
        return "raw uiomove continuation stack collision";
    case MD_RAW_BRIDGE_ERROR_PENDING_EXHAUSTED:
        return "raw uiomove continuation slots exhausted";
    case MD_RAW_BRIDGE_ERROR_STALE_COMPLETION:
        return "stale raw uiomove completion";
    case MD_RAW_BRIDGE_ERROR_MALFORMED_COMPLETION:
        return "malformed raw uiomove completion";
    case MD_RAW_BRIDGE_ERROR_UIOMOVE:
        return "guest uiomove returned an error";
    case MD_RAW_BRIDGE_ERROR_BLOCK_IO: return "block transfer failed";
    default: return "unknown raw bridge error";
    }
}
