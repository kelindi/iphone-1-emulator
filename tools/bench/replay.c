/* Snapshot replay benchmark. Build against the core being measured:
 * cc -O3 -Icore/include tools/bench/replay.c BUILD/core/libemucore.a -lm -o replay
 * replay SNAPSHOT INSTRUCTIONS [REPEATS [DIRECT_WRITES [FINAL_SNAPSHOT]]]
 * Loading, saving and hashing are outside the measured execution interval.
 */
#define _POSIX_C_SOURCE 200809L
#include "board.h"
#include "snapshot.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) abort();
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 6) return 2;
    const uint64_t count = strtoull(argv[2], NULL, 0);
    unsigned repeats = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0) : 1u;
    bool direct = argc > 4 && strtoul(argv[4], NULL, 0) != 0;
    s5l8900_t *m = calloc(1, sizeof *m);
    const s5l_board_t *board = s5l_board_find("m68");
    if (!m || !board || !count || !repeats ||
        !s5l_board_init(board, m, 256u << 20)) return 1;
    for (unsigned trial = 0; trial < repeats; trial++) {
        snapshot_status_t ss = snapshot_load(m, argv[1]);
        if (ss != SNAP_OK) {
            fprintf(stderr, "%s\n", snapshot_strerror(ss));
            return 1;
        }
        if (!s5l8900_set_direct_ram_writes(m, direct)) return 1;
        uint64_t retired = 0;
        arm_status_t status = ARM_OK;
        double began = now();
        while (retired < count) {
            unsigned slice = count - retired > 1000000u ? 1000000u :
                             (unsigned)(count - retired);
            unsigned n = s5l8900_run(m, slice, &status);
            retired += n;
            if (!n || status != ARM_OK) break;
        }
        double seconds = now() - began;
        printf("trial=%u direct=%u retired=%" PRIu64 " seconds=%.6f mips=%.3f status=%d pc=%08x cycles=%" PRIu64 "\n",
               trial + 1u, direct, retired, seconds, retired / seconds / 1e6,
               status, m->cpu.r[15], m->cpu.cycles);
        fflush(stdout);
        if (retired != count || status != ARM_OK) return 1;
    }
    if (argc > 5 && snapshot_save(m, argv[5]) != SNAP_OK) return 1;
    s5l8900_free(m);
    free(m);
    return 0;
}
