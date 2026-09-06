#ifndef ISOLATED_S5L8900_SDIO_H
#define ISOLATED_S5L8900_SDIO_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Probe-only model: no machine structure or snapshot format changes. */
enum {
    SDIO_BASE = 0x38d00000u, SDIO_SIZE = 0x1000u,
    SDIO_CTRL = 0x00, SDIO_DCTRL = 0x04, SDIO_CMD = 0x08,
    SDIO_ARG = 0x0c, SDIO_STATE = 0x10, SDIO_ACK = 0x14,
    SDIO_STATUS = 0x18, SDIO_RESP0 = 0x20, SDIO_CLKDIV = 0x30,
    SDIO_CSR = 0x34, SDIO_IRQ = 0x38, SDIO_IRQMASK = 0x3c,
    SDIO_BUFFER = 0x44, SDIO_BLOCK_LENGTH = 0x48, SDIO_BLOCK_COUNT = 0x4c,
    SDIO_ENDPOINT_CAPACITY = 1024, SDIO_HELPER_CAPACITY = 4096,
    SDIO_READY = 1u, SDIO_SENT = 4u, SDIO_DONE = 0x10u,
    /* Apple branches on bit15 as an error; precise silicon error taxonomy
     * is not claimed. This probe uses it for absent/unsupported responses. */
    SDIO_RESPONSE_ERROR = 0x8000u,
    SDIO_EXECUTE = 0x80000000u,
    SDIO_R5_COMMAND_STATE = 0x1000u, SDIO_R5_ERROR = 0x0800u,
    SDIO_R5_FUNCTION = 0x0200u, SDIO_R5_ADDRESS = 0x0100u
};
typedef enum { CARD_IDLE, CARD_READY, CARD_STANDBY, CARD_SELECTED } sdio_card_state_t;
typedef struct {
    bool present;
    bool enumeration; /* explicit additional CCCR/FBR policy */
    bool boot_endpoint;
    bool data_transfer; /* narrowly admitted helper framing */
    bool helper_receiver; /* receive/storage only; never executes helper */
    bool complete_main; /* isolated full-container receive, no handoff execution */
    bool helper_semantics; /* opt-in model of the exact supported program, not CPU execution */
    uint64_t semantic_ticks; /* declared processing delay */
    uint16_t helper_capacity; /* explicit emulated download memory capacity */
    uint64_t data_ticks;
    uint16_t endpoint_capacity; /* zero means no storage available */
    uint32_t io_port;
    uint64_t function_init_ticks;
    uint8_t io_functions;
    uint16_t assigned_rca;
    uint32_t voltage_mask;
    /* Explicit model policy, not measured Marvell register/identity bytes.
     * Unsupported bytes produce R5 address errors; no implicit zero-filled
     * CCCR/CIS/firmware-ready window. Only function-zero reads are modeled. */
    uint16_t readable_cccr;
    uint8_t cccr[12];
    uint64_t command_ticks;
} sdio_config_t;
typedef struct {
    sdio_card_state_t state;
    uint16_t rca;
    uint32_t selected_voltage;
    uint8_t interrupt_enable, bus_interface;
    uint16_t block_size[2];
} sdio_card_t;
typedef struct {
    bool initialized, enabled, ready, initializing, host_power;
    uint64_t ready_deadline;
    uint16_t capacity, used;
    uint8_t configuration, interrupt_mask, interrupt_pending;
    uint8_t receive[SDIO_ENDPOINT_CAPACITY];
} sdio_endpoint_t;
typedef struct {
    uint16_t used;
    uint32_t frames;
    bool terminated, framing_stalled;
    uint8_t memory[SDIO_HELPER_CAPACITY];
} sdio_helper_t;
typedef enum {
    MAIN_OFF, MAIN_BOOT_PENDING, MAIN_HEADER, MAIN_HEADER_PENDING,
    MAIN_BODY, MAIN_BODY_PENDING, MAIN_STOPPED, MAIN_HANDOFF
} sdio_main_phase_t;
typedef struct {
    sdio_main_phase_t phase;
    uint16_t requested, pending_length;
    uint32_t header[4];
    uint8_t pending[512]; /* committed card-side bytes, independent of host DMA/FIFO */
    uint8_t memory[512]; /* only c0001000..c00011ff exists in this bounded model */
    uint8_t low_before[16], low[0x17000], low_after[16];
    uint8_t high_before[16], high[0x6000], high_after[16];
    uint8_t low_coverage[0x17000], high_coverage[0x6000];
    uint64_t deadline, publications, headers, bodies, copied, errors;
    uint64_t length_reads[2];
    bool waiting_deadline;
    unsigned diagnostic; /* 1 bounds/form, 2 command4 handoff, 3 clock overflow */
} sdio_main_t;
typedef const uint8_t *(*sdio_dma_reader_t)(void *, uint32_t, size_t);
typedef struct {
    bool armed, active;
    uint32_t address, port;
    uint16_t length;
    uint64_t deadline;
    uint8_t staging[512];
} sdio_data_t;
typedef struct {
    sdio_config_t config;
    sdio_card_t card;
    sdio_endpoint_t endpoint;
    sdio_data_t data;
    sdio_helper_t helper;
    sdio_main_t main_model;
    uint64_t helper_protocol_errors; /* host diagnosis, no invented CRC bit */
    sdio_dma_reader_t dma_reader; /* host-only; never portable state */
    void *dma_ctx;
    uint64_t data_starts, data_completions, data_errors;
    uint32_t buffer_address, block_length, block_count;
    uint32_t control, data_control, command, argument, events;
    uint32_t response[4], clock_divider, csr, irq_pending, irq_mask;
    uint32_t pending_command, pending_argument;
    uint64_t now, deadline;
    bool busy, have_time;
    /* Host diagnostics only. No serialization or emulated hardware aliases. */
    uint64_t triggers, completions, transport_errors, rejected_writes;
    uint64_t cccr_reads, response_reads;
    uint64_t iorx_reads, iorx_wait_reads, iorx_ready_reads, function_reads;
    uint16_t cccr_read_mask;
} sdio_t;
bool sdio_init(sdio_t *, const sdio_config_t *);
void sdio_host_reset(sdio_t *);
void sdio_card_reset(sdio_t *);
void sdio_step(sdio_t *, uint64_t now);
uint32_t sdio_read(sdio_t *, uint32_t offset, unsigned width, uint64_t now);
bool sdio_write(sdio_t *, uint32_t offset, uint32_t value, unsigned width, uint64_t now);
void sdio_bind_dma_reader(sdio_t *, sdio_dma_reader_t, void *);
bool sdio_irq_level(const sdio_t *);
bool sdio_endpoint_can_receive(const sdio_t *, uint32_t port, size_t length);
bool sdio_endpoint_receive(sdio_t *, uint32_t port, const uint8_t *, size_t length);
#endif
