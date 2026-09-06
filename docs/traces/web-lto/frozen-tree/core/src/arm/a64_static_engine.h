/* Internal bridge between the SoC run loop and the optional signed engine. */
#ifndef S5LBOX_A64_STATIC_ENGINE_H
#define S5LBOX_A64_STATIC_ENGINE_H

#include "soc.h"

/* The generic engine initializes at sixteen. The iOS product explicitly uses
 * this larger ceiling after the exact Apple-host gate. Deterministic execution
 * clamps each invocation to the first timebase edge; optional active host
 * timing may use the whole ceiling because elapsed edges advance together at
 * the next bounded host sample. */
#define S5LBOX_STATIC_A64_PRODUCT_CHAIN_INSNS 256u

/* Cheap execution-policy gate. In the LTO iOS product this folds to the
 * opaque-state pointer and its first enabled byte, allowing a compiled but
 * disabled engine to avoid the much larger timebase/input eligibility path. */
bool s5l8900_static_a64_is_enabled(const s5l8900_t *m);

/* A privileged compact interval may reach another proven FETCH window before
 * its budget is exhausted. The machine loop owns the device/clock boundary
 * that must precede that continuation. The callback accounts exactly the
 * native retirements since its previous invocation and returns whether the
 * ordinary machine gate still permits resident execution. */
typedef bool (*s5l8900_static_a64_retirement_boundary_fn)(
    void *opaque, unsigned retired);

/* Try an already-translated, time-bounded chain. Each decoded head remains at
 * most sixteen guest instructions, stays inside the current proven fetch block
 * and repeats the cache/raw-byte witness. The configured total cannot exceed
 * the product ceiling above. Zero means fall back to the architectural
 * interpreter without changing guest state. When a positive exact prefix ends
 * at an unchanged decoded negative, or a privileged compact interval leaves
 * its fallback instruction untouched, `known_negative` reports that the next
 * signed probe is redundant. `boundary_retired` reports the prefix already
 * accounted through `retirement_boundary`; the caller must account only the
 * remainder after this function returns. */
unsigned s5l8900_static_a64_try(s5l8900_t *m, unsigned max_insns,
                                bool *known_negative,
                                arm_status_t *status,
                                s5l8900_static_a64_retirement_boundary_fn
                                    retirement_boundary,
                                void *retirement_opaque,
                                unsigned *boundary_retired);

/* One exact architectural fallback for the resident compact loop. Implemented
 * beside the SoC run loop because only machine.c owns the complete
 * level/input boundary. Return 0 for no retirement, 1 to continue the bounded
 * resident interval, or 2 after one retirement that must return to the device
 * tick immediately. */
unsigned s5l8900_static_a64_fallback_step(s5l8900_t *m,
                                          arm_status_t *status);

/* Revalidate and account a known-negative probe bypass after the device tick.
 * A compact pending witness is single-use; decoded negatives remain governed
 * by their complete cache witness. False leaves the ordinary next-loop
 * decision untouched. */
bool s5l8900_static_a64_commit_known_negative_bypass(s5l8900_t *m);

/* Bracket one public s5l8900_run() call for the explicit compact-PC sampler.
 * False means the marker was not enabled and the caller performs no matching
 * end operation. This outer-slice gate is not a per-instruction cost. The
 * sampler thread is created only at opt-in boot; begin records the exact Mach
 * thread executing this slice and end makes it ineligible. Retained outside-
 * runner PCs therefore still belong to the emulator pthread rather than an
 * arbitrary process thread. */
bool s5l8900_static_a64_compact_raw_pc_profile_slice_begin(
    const s5l8900_t *m);
void s5l8900_static_a64_compact_raw_pc_profile_slice_end(
    const s5l8900_t *m);

/* Release the per-machine decode cache. Safe on an uninitialised NULL field. */
void s5l8900_static_a64_dispose(s5l8900_t *m);

#endif /* S5LBOX_A64_STATIC_ENGINE_H */
