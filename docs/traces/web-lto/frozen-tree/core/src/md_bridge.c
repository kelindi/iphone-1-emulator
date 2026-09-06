/*
 * S5LBox -- firmware-parameterized host-backed memory-disk bridge.
 *
 * The patched guest calls the original bcopy_phys ABI:
 *   source      r1:r0
 *   destination r3:r2
 *   length      *(uint32_t *)sp
 * Only the two exact configured Thumb SVC instructions are recognized.  Each
 * replaces the first halfword of a 32-bit Thumb BL at a site whose LR value
 * after the bypassed callee was audited dead.  The bridge stays register-pure;
 * it must never be generalized to an unaudited caller of the same routine.
 */
#include "md_bridge.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MD_BRIDGE_ADDRESS_SPACE_SIZE (UINT64_C(1) << 32)

static uint64_t add_saturating_u64(uint64_t a, uint64_t b) {
    return UINT64_MAX - a < b ? UINT64_MAX : a + b;
}

static void increment_saturating_u64(uint64_t *value) {
    if (*value != UINT64_MAX)
        (*value)++;
}

static bool range_end_32(uint64_t base, uint64_t size, uint64_t *end) {
    if (size == 0u || base >= MD_BRIDGE_ADDRESS_SPACE_SIZE ||
        size > MD_BRIDGE_ADDRESS_SPACE_SIZE - base)
        return false;
    *end = base + size;
    return true;
}

static bool token_range_end(uint64_t base, uint64_t size, uint64_t *end) {
    if (size == 0u || base >= MD_BRIDGE_ADDRESS_SPACE_SIZE ||
        size > UINT64_MAX - base)
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

static bool range_fits_page(uint64_t address, uint32_t length) {
    uint64_t page_offset = address & (MD_BRIDGE_PAGE_SIZE - UINT64_C(1));
    return (uint64_t)length <= MD_BRIDGE_PAGE_SIZE - page_offset;
}

bool md_bridge_config_valid(const md_bridge_config_t *config) {
    uint64_t token_end;
    uint64_t ram_end;

    if (config == NULL || config->ram == NULL || config->block == NULL ||
        config->block->read_at == NULL ||
        config->block->write_at == NULL)
        return false;
    /* Each site owns both halfwords of one replaced Thumb BL. */
    if (config->read_site.pc > UINT32_MAX - 2u ||
        config->write_site.pc > UINT32_MAX - 2u ||
        ((uint64_t)config->read_site.pc <
             (uint64_t)config->write_site.pc + 4u &&
         (uint64_t)config->write_site.pc <
             (uint64_t)config->read_site.pc + 4u))
        return false;
    if ((config->read_site.pc & 1u) != 0u ||
        (config->write_site.pc & 1u) != 0u)
        return false;
    if (config->read_site.encoding > UINT16_MAX ||
        config->write_site.encoding > UINT16_MAX ||
        (config->read_site.encoding & 0xff00u) != 0xdf00u ||
        (config->write_site.encoding & 0xff00u) != 0xdf00u ||
        config->read_site.encoding == config->write_site.encoding)
        return false;
    if ((config->token_base & (MD_BRIDGE_PAGE_SIZE - UINT64_C(1))) != 0u ||
        (config->media_size & (MD_BRIDGE_PAGE_SIZE - UINT64_C(1))) != 0u ||
        (config->ram_base & (MD_BRIDGE_PAGE_SIZE - UINT64_C(1))) != 0u ||
        (config->ram_size & (MD_BRIDGE_PAGE_SIZE - UINT64_C(1))) != 0u)
        return false;
#if SIZE_MAX < UINT64_MAX
    if (config->ram_size > (uint64_t)SIZE_MAX)
        return false;
#endif
    if (!token_range_end(config->token_base, config->media_size, &token_end) ||
        !range_end_32(config->ram_base, config->ram_size, &ram_end))
        return false;
    if (config->token_base < ram_end && config->ram_base < token_end)
        return false;
    if (config->block->size != config->media_size)
        return false;
    return true;
}

static md_bridge_direction_t identify_site(const md_bridge_config_t *config,
                                            uint32_t pc,
                                            uint32_t encoding) {
    if (config == NULL)
        return MD_BRIDGE_DIRECTION_NONE;
    if (pc == config->read_site.pc &&
        encoding == config->read_site.encoding)
        return MD_BRIDGE_DIRECTION_READ;
    if (pc == config->write_site.pc &&
        encoding == config->write_site.encoding)
        return MD_BRIDGE_DIRECTION_WRITE;
    return MD_BRIDGE_DIRECTION_NONE;
}

static arm_svc_result_t fail(md_bridge_t *bridge,
                             md_bridge_error_t *error,
                             md_bridge_error_code_t code) {
    error->code = code;
    bridge->last_error = *error;
    increment_saturating_u64(&bridge->stats.failures);
    return ARM_SVC_ERROR;
}

void md_bridge_init(md_bridge_t *bridge, const md_bridge_config_t *config) {
    md_bridge_config_t saved_config = {0};

    if (bridge == NULL)
        return;
    if (config != NULL)
        saved_config = *config;
    *bridge = (md_bridge_t){0};
    if (config != NULL)
        bridge->config = saved_config;
}

arm_svc_result_t md_bridge_handle_svc(void *context, arm_cpu_t *cpu,
                                      uint32_t pc, uint32_t encoding) {
    md_bridge_t *bridge = (md_bridge_t *)context;
    md_bridge_direction_t direction;
    md_bridge_error_t error;
    const md_bridge_config_t *config;
    uint32_t mode;
    uint32_t stack_va;
    uint32_t stack_pa = 0u;
    uint32_t mmu_status;
    uint64_t source;
    uint64_t destination;
    uint64_t token_address;
    uint64_t guest_address;
    uint64_t media_offset;
    uint32_t length;
    size_t transferred = 0u;
    vm_block_status_t block_status;
    size_t ram_offset;

    if (bridge == NULL)
        return ARM_SVC_ERROR;
    config = &bridge->config;
    direction = identify_site(config, pc, encoding);
    if (direction == MD_BRIDGE_DIRECTION_NONE)
        return ARM_SVC_UNHANDLED;

    error = (md_bridge_error_t){0};
    error.direction = direction;
    error.pc = pc;
    error.encoding = encoding;

    if (cpu == NULL)
        return fail(bridge, &error, MD_BRIDGE_ERROR_NULL_CPU);

    /* Direct callers receive the same defence as arm_step's outer guard. */
    if ((cpu->cpsr & ARM_CPSR_T) == 0u)
        return ARM_SVC_UNHANDLED;
    mode = cpu->cpsr & ARM_CPSR_MODE_MASK;
    if (mode == ARM_MODE_USR)
        return ARM_SVC_UNHANDLED;
    if (!arm_mode_is_valid(mode))
        return fail(bridge, &error, MD_BRIDGE_ERROR_INVALID_MODE);

    if (!md_bridge_config_valid(config))
        return fail(bridge, &error, MD_BRIDGE_ERROR_INVALID_CONFIG);
    if ((cpu->cp15.sctlr & ARM_SCTLR_M) != 0u &&
        (cpu->bus == NULL || cpu->bus->read32 == NULL))
        return fail(bridge, &error, MD_BRIDGE_ERROR_MISSING_BUS_ACCESS);

    stack_va = cpu->r[13];
    error.stack_va = stack_va;
    if (!range_fits_page(stack_va, (uint32_t)sizeof(uint32_t)))
        return fail(bridge, &error, MD_BRIDGE_ERROR_STACK_PAGE);
    if ((stack_va & 3u) != 0u)
        return fail(bridge, &error, MD_BRIDGE_ERROR_STACK_ALIGNMENT);

    mmu_status = arm_mmu_translate(cpu, stack_va, ARM_ACCESS_READ, true,
                                   &stack_pa);
    error.stack_pa = stack_pa;
    error.mmu_status = mmu_status;
    if (mmu_status != 0u)
        return fail(bridge, &error, MD_BRIDGE_ERROR_STACK_TRANSLATION);
    if (!range_contains(config->ram_base, config->ram_size,
                        stack_pa, sizeof(uint32_t)))
        return fail(bridge, &error, MD_BRIDGE_ERROR_STACK_RANGE);

    ram_offset = (size_t)((uint64_t)stack_pa - config->ram_base);
    length = config->ram[ram_offset];
    length |= (uint32_t)config->ram[ram_offset + 1u] << 8;
    length |= (uint32_t)config->ram[ram_offset + 2u] << 16;
    length |= (uint32_t)config->ram[ram_offset + 3u] << 24;
    source = ((uint64_t)cpu->r[1] << 32) | cpu->r[0];
    destination = ((uint64_t)cpu->r[3] << 32) | cpu->r[2];
    error.length = length;
    error.source = source;
    error.destination = destination;

    if (length == 0u || length > MD_BRIDGE_MAX_TRANSFER)
        return fail(bridge, &error, MD_BRIDGE_ERROR_LENGTH);

    if (direction == MD_BRIDGE_DIRECTION_READ) {
        token_address = source;
        guest_address = destination;
    } else {
        guest_address = source;
        token_address = destination;
    }

    /*
     * mdevstrategy reconstructs its page base plus byte offset as a 64-bit
     * bcopy_phys operand. A host-backed token may therefore cross 4 GiB. The
     * other operand is real guest RAM and must never escape the 32-bit machine.
     */
    if ((guest_address >> 32) != 0u)
        return fail(bridge, &error, MD_BRIDGE_ERROR_ADDRESS_HIGH);

    if (!range_contains(config->token_base, config->media_size,
                        token_address, length))
        return fail(bridge, &error, MD_BRIDGE_ERROR_TOKEN_RANGE);
    if (!range_contains(config->ram_base, config->ram_size,
                        guest_address, length))
        return fail(bridge, &error, MD_BRIDGE_ERROR_GUEST_RANGE);
    if (!range_fits_page(token_address, length))
        return fail(bridge, &error, MD_BRIDGE_ERROR_TOKEN_PAGE);
    if (!range_fits_page(guest_address, length))
        return fail(bridge, &error, MD_BRIDGE_ERROR_GUEST_PAGE);

    media_offset = token_address - config->token_base;
    error.media_offset = media_offset;

    if (direction == MD_BRIDGE_DIRECTION_READ) {
        if (config->block->read_at == NULL)
            return fail(bridge, &error, MD_BRIDGE_ERROR_INVALID_CONFIG);
        block_status = vm_block_read_exact(config->block, media_offset,
                                           bridge->scratch, length,
                                           config->cancelled,
                                           config->cancel_context,
                                           &transferred);
        error.block_status = block_status;
        error.transferred = transferred;
        if (block_status != VM_BLOCK_STATUS_OK)
            return fail(bridge, &error, MD_BRIDGE_ERROR_BLOCK_IO);

        /* Publish only after the complete block is safely staged. */
        ram_offset = (size_t)(guest_address - config->ram_base);
        memcpy(config->ram + ram_offset, bridge->scratch, length);
        increment_saturating_u64(&bridge->stats.successful_reads);
        bridge->stats.bytes_read =
            add_saturating_u64(bridge->stats.bytes_read, length);
    } else {
        if (config->block->write_at == NULL)
            return fail(bridge, &error, MD_BRIDGE_ERROR_INVALID_CONFIG);
        ram_offset = (size_t)(guest_address - config->ram_base);
        memcpy(bridge->scratch, config->ram + ram_offset, length);

        block_status = vm_block_write_exact(config->block, media_offset,
                                            bridge->scratch, length,
                                            config->cancelled,
                                            config->cancel_context,
                                            &transferred);
        error.block_status = block_status;
        error.transferred = transferred;
        if (block_status != VM_BLOCK_STATUS_OK)
            return fail(bridge, &error, MD_BRIDGE_ERROR_BLOCK_IO);

        increment_saturating_u64(&bridge->stats.successful_writes);
        bridge->stats.bytes_written =
            add_saturating_u64(bridge->stats.bytes_written, length);
    }

    return ARM_SVC_HANDLED;
}

const char *md_bridge_error_string(md_bridge_error_code_t code) {
    switch (code) {
    case MD_BRIDGE_ERROR_NONE:              return "no error";
    case MD_BRIDGE_ERROR_NULL_CPU:          return "null CPU";
    case MD_BRIDGE_ERROR_INVALID_MODE:      return "invalid CPU mode";
    case MD_BRIDGE_ERROR_INVALID_CONFIG:    return "invalid bridge configuration";
    case MD_BRIDGE_ERROR_MISSING_BUS_ACCESS:return "missing required bus access";
    case MD_BRIDGE_ERROR_STACK_ALIGNMENT:   return "unaligned service stack";
    case MD_BRIDGE_ERROR_STACK_PAGE:        return "service stack word crosses a page";
    case MD_BRIDGE_ERROR_STACK_TRANSLATION: return "service stack translation failed";
    case MD_BRIDGE_ERROR_STACK_RANGE:       return "service stack is outside RAM";
    case MD_BRIDGE_ERROR_ADDRESS_HIGH:      return "guest RAM bcopy address is not 32-bit";
    case MD_BRIDGE_ERROR_LENGTH:            return "invalid transfer length";
    case MD_BRIDGE_ERROR_TOKEN_RANGE:       return "media token range is invalid";
    case MD_BRIDGE_ERROR_GUEST_RANGE:       return "guest RAM range is invalid";
    case MD_BRIDGE_ERROR_TOKEN_PAGE:        return "media transfer crosses a page";
    case MD_BRIDGE_ERROR_GUEST_PAGE:        return "guest transfer crosses a page";
    case MD_BRIDGE_ERROR_BLOCK_IO:          return "block transfer failed";
    default:                                return "unknown bridge error";
    }
}
