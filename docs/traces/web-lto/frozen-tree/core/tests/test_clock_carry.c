/* Independent elapsed-time conservation oracle. All synchronization is reached
 * through the public machine run/tick APIs; no private implementation is used. */
#include "soc.h"
#include "snapshot.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_PER_SECOND UINT64_C(1000000000)
static unsigned failures, checks;
#define REQUIRE(condition, message) do { \
    checks++; \
    if (!(condition)) { \
        failures++; \
        printf("FAIL %s:%d: %s\n", __func__, __LINE__, message); \
    } \
} while (0)

typedef struct {
    s5l8900_t *machine;
    uint64_t now, last_ticks, max_delta;
    bool succeeds;
} host_t;

static bool sample_now(void *opaque, uint64_t *ns) {
    host_t *host = opaque;
    uint64_t ticks = host->machine->timer.ticks;
    if (ticks >= host->last_ticks && ticks - host->last_ticks > host->max_delta)
        host->max_delta = ticks - host->last_ticks;
    host->last_ticks = ticks;
    if (!host->succeeds) return false;
    *ns = host->now;
    return true;
}

static void run_one(s5l8900_t *machine) {
    arm_status_t status = ARM_OK;
    unsigned count = s5l8900_run(machine, 1, &status);
    REQUIRE(count == 1 && status == ARM_OK, "real branch did not retire");
}

static void initialize(s5l8900_t *machine, host_t *host, uint32_t hz) {
    memset(machine, 0, sizeof *machine);
    if (!s5l8900_init(machine, 0, 1u << 20)) {
        fprintf(stderr, "Machine initialization failed\n");
        exit(1);
    }
    const uint32_t branch = 0xeafffffeu;
    s5l8900_load(machine, 0, &branch, sizeof branch);
    machine->cpu_hz = machine->tb_hz = machine->pmu.tick_hz = hz;
    machine->cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    *host = (host_t){.machine = machine, .now = NS_PER_SECOND, .succeeds = true};
    REQUIRE(s5l8900_set_active_host_clock(machine, sample_now, host), "enable callback");
    REQUIRE(s5l8900_set_active_clock_carry(machine, true), "enable carry");
    run_one(machine);
    REQUIRE(machine->timer.ticks == 0, "initial anchor manufactured ticks");
}

static void drain(s5l8900_t *machine) {
    unsigned count = 0;
    while (machine->active_clock_debt_ticks && count++ < 100000)
        run_one(machine);
    REQUIRE(!machine->active_clock_debt_ticks, "stationary host cannot drain debt");
}

/* Splitting whole seconds keeps the independent rational oracle in portable
 * uint64_t arithmetic even at UINT32_MAX Hz. These intervals are under 30s. */
static uint64_t elapsed_ticks(uint64_t ns, uint32_t hz) {
    return (ns / NS_PER_SECOND) * hz +
           ((ns % NS_PER_SECOND) * hz) / NS_PER_SECOND;
}

static void conservation(uint32_t hz) {
    s5l8900_t machine;
    host_t host;
    initialize(&machine, &host, hz);
    uint64_t elapsed = 0, random = 1234567;
    for (unsigned i = 0; i < 600; i++) {
        random = random * UINT64_C(6364136223846793005) + 1;
        uint64_t delta = (random >> 32) % 19000001u;
        if (i % 100 == 0) delta += UINT64_C(2500000000);
        host.now += delta;
        elapsed += delta;
        if (i % 7 == 0) {
            uint64_t observed = elapsed_ticks(delta, hz) / 2;
            if (observed > UINT32_MAX) observed = UINT32_MAX;
            /* Already observed paced-WFI time must be counted exactly once. */
            s5l8900_tick(&machine, (uint32_t)observed);
            host.last_ticks = machine.timer.ticks;
        }
        run_one(&machine);
        REQUIRE(machine.timer.ticks + machine.active_clock_debt_ticks == elapsed_ticks(elapsed, hz),
                "elapsed differs from paid plus debt after observed WFI");
        REQUIRE(machine.active_clock_fraction == ((elapsed % NS_PER_SECOND) * hz) % NS_PER_SECOND,
                "fractional elapsed tick lost");
    }
    drain(&machine);
    REQUIRE(machine.timer.ticks == elapsed_ticks(elapsed, hz), "final conservation");
    REQUIRE(host.max_delta <= ((uint64_t)hz * 8) / 1000, "device sync exceeded 8ms");
    printf("conservation hz=%u elapsed_ns=%" PRIu64 " ticks=%" PRIu64
           " max_delta=%" PRIu64 "\n", hz, elapsed, machine.timer.ticks, host.max_delta);
    s5l8900_free(&machine);
}

static void lifecycle(void) {
    s5l8900_t machine;
    host_t host;
    initialize(&machine, &host, 1000);
    host.now += 100000000;
    run_one(&machine);
    REQUIRE(machine.active_clock_debt_ticks == 84, "100ms leaves 84ms after two syncs");
    uint64_t ticks = machine.timer.ticks;
    REQUIRE(s5l8900_set_active_host_clock(&machine, NULL, NULL), "disable callback");
    REQUIRE(!machine.active_clock_debt_ticks && !machine.active_clock_fraction &&
            !machine.active_clock_anchor_valid, "Pause retained transient clock state");
    host.now += UINT64_C(3600000000000);
    REQUIRE(s5l8900_set_active_host_clock(&machine, sample_now, &host), "resume callback");
    run_one(&machine);
    REQUIRE(machine.timer.ticks == ticks, "one-hour Pause manufactured ticks");
    host.now += 100000000;
    run_one(&machine);
    ticks = machine.timer.ticks;
    host.now--;
    run_one(&machine);
    REQUIRE(machine.timer.ticks == ticks + 1 && !machine.active_clock_debt_ticks &&
            !machine.active_clock_anchor_valid, "backwards host did not clear debt and fall back exactly");
    host.now += 2;
    run_one(&machine);
    host.now += 100000000;
    run_one(&machine);
    ticks = machine.timer.ticks;
    host.succeeds = false;
    run_one(&machine);
    REQUIRE(machine.timer.ticks == ticks + 1 && !machine.active_clock_debt_ticks &&
            !machine.active_clock_anchor_valid, "failed sample did not clear debt and fall back exactly");
    s5l8900_free(&machine);
}

static void overflow_and_low_rates(void) {
    s5l8900_t machine;
    host_t host;
    initialize(&machine, &host, UINT32_MAX);
    host.now = UINT64_MAX;
    run_one(&machine);
    REQUIRE(!machine.active_clock_debt_ticks && !machine.active_clock_anchor_valid &&
            machine.active_clock_failures == 1 && machine.timer.ticks == 1,
            "unrepresentable interval did not fail closed");
    s5l8900_free(&machine);
    initialize(&machine, &host, 125);
    REQUIRE(s5l8900_set_active_clock_carry(&machine, false), "disable carry");
    machine.cpu_hz = 124;
    REQUIRE(!s5l8900_set_active_clock_carry(&machine, true) && !machine.active_clock_carry,
            "124Hz enabled zero-payment carry");
    machine.cpu_hz = 1;
    REQUIRE(!s5l8900_set_active_clock_carry(&machine, true) && !machine.active_clock_carry,
            "1Hz enabled zero-payment carry");
    s5l8900_free(&machine);
}

static void cpi_change(void) {
    s5l8900_t machine;
    host_t host;
    initialize(&machine, &host, S5L8900_CPU_HZ);
    machine.tb_hz = machine.pmu.tick_hz = 6000000;
    host.now += 100000000;
    run_one(&machine);
    REQUIRE(!s5l8900_set_cycles_per_insn(&machine, 16), "live CPI change accepted");
    drain(&machine);
    REQUIRE(machine.timer.ticks == 600000, "CPI change inflated or lost pending elapsed");
    s5l8900_free(&machine);
}

static void snapshots_and_default_policy(void) {
    s5l8900_t machine;
    host_t host;
    initialize(&machine, &host, 1000);
    host.now += 100000000;
    run_one(&machine);
    REQUIRE(machine.active_clock_debt_ticks == 84, "snapshot fixture has no debt");
    uint8_t *first = NULL, *second = NULL;
    size_t first_size = 0, second_size = 0;
    snapshot_status_t saved = snapshot_save_mem(&machine, &first, &first_size);
    REQUIRE(saved == SNAP_OK, "first snapshot failed");
    machine.active_clock_debt_ticks = 0;
    snapshot_status_t saved_second = snapshot_save_mem(&machine, &second, &second_size);
    REQUIRE(saved_second == SNAP_OK, "second snapshot failed");
    if (saved == SNAP_OK && saved_second == SNAP_OK) {
        REQUIRE(first_size == second_size && !memcmp(first, second, first_size),
                "host-only debt changed serialized guest snapshot bytes");
        machine.active_clock_debt_ticks = 1234;
        REQUIRE(snapshot_load_mem(&machine, first, first_size) == SNAP_OK, "snapshot restore failed");
        REQUIRE(machine.active_clock_carry && machine.active_host_now == sample_now &&
                !machine.active_clock_debt_ticks && !machine.active_clock_anchor_valid,
                "restore did not preserve host policy and clear transient debt");
        uint64_t ticks = machine.timer.ticks;
        host.now += UINT64_C(3600000000000);
        run_one(&machine);
        REQUIRE(machine.timer.ticks == ticks, "restore included inactive elapsed time");
    }
    free(first);
    free(second);
    REQUIRE(s5l8900_set_active_clock_carry(&machine, false), "disable carry");
    uint64_t ticks = machine.timer.ticks;
    host.now += 100000000;
    run_one(&machine);
    REQUIRE(machine.timer.ticks == ticks + 8 && !machine.active_clock_debt_ticks,
            "legacy default discard policy changed");
    run_one(&machine);
    REQUIRE(machine.timer.ticks == ticks + 8, "legacy default secretly retained debt");
    s5l8900_free(&machine);
}

int main(void) {
    conservation(125);
    conservation(1001);
    conservation(6000000);
    conservation(UINT32_MAX);
    lifecycle();
    overflow_and_low_rates();
    cpi_change();
    snapshots_and_default_policy();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
