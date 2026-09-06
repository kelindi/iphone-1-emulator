# Isolated ARM decoder partition

This candidate targets actual interpreted fallback execution. The accepted production browser attribution places 29.09% of warm Settings worker self samples in `arm_step`, compared with 12.22% in fused `emu_run` and 6.37% in `region_find`. This is function-level attribution, not proof that decoding accounts for all of `arm_step`. The shipping WAT body is 12,152 lines with 36 locals; instruction handlers are heavily inlined.

The candidate partitions the existing conditional ARM selection chain by bits 27:26:

| Class | Existing handlers, original order |
|---|---|
| 00 | Data processing, miscellaneous, multiply, extra transfers, exclusive/SWP, WFI, PSR |
| 01 | Media, then ordinary single transfers |
| 10 | Branch, then block transfer |
| 11 | Coprocessor, VFP, SVC |

All 24 predicate/handler fragments are reused verbatim except indentation, in their original within-class order. The prefix containing fetch, mode/interrupt validation, cycles and conditional execution is byte-identical. So is the common data-abort/undefined/PC epilogue. Thumb and unconditional cond=0xF execution are untouched. The original compiler, region admissions and JIT guards are unchanged. This is not a reopened coverage/cache/POP experiment.

The anticipated mechanism is to avoid testing impossible instruction families on ordinary interpreted loads/stores, block transfers and coprocessor instructions. A switch also adds cost to some paths and may alter host compiler layout. No numerical speed prediction is made; the whole `arm_step` percentage is not an attainable saving estimate.

Selection projection checks pass **2,097,185 cases**. Apart from the exact full-word WFI match, all guards use only 16 instruction bits; every projection is checked for all 16 condition fields with ignored bits all-zero/all-one. WFI and its 32 single-bit mutations are checked explicitly. The selection and fragment-generation sources are retained. The deliberately incorrect media/single-transfer order fails on `06000010` (old handler 5, wrong candidate 6), proving the oracle catches the overlapping class. Existing native ARM tests pass **1,067 checks** against the candidate. The isolated WASM compiles from the production archive with only the ARM interpreter object replaced; build commands and hashes are retained.

Both genuine fixtures now pass100M-instruction candidate interpreter/original-seed runs against preserved original603679 references: four complete raw snapshots match exactly, with unchanged TLB and original generated retirement/call/region counters. The MBX hash is `d4ca9be9…e38c`; software is recorded in [correctness.json](correctness.json). No byte normalization is used for acceptance. Longer replay and quiet timing remain pending. Production remains unchanged, and this is not a speed milestone.


The longer correctness gate now passes **600M instructions per engine and fixture**: candidate interpreter and original-seed execution on both MBX and software fixtures preserve all **24 complete raw checkpoints**, TLB values and original generated retirement/call/region counts. No normalized hash is used for acceptance. This proof used the frozen603679 core before the separate snapshot-classifier promotion; a fresh decoder-only variant over that new production classifier is checked again before timing.

An additional [independent final-artifact audit](audit.json) parses actual original/candidate C without importing the transformation's groups. All guard/body token sequences and within-class precedence match; byte-level prefix/epilogue identity is checked. It proves that every `insn` use in every non-WFI predicate is within the enumerated16-bit mask. Neither all-zero nor all-one ignored bits can equal WFI's ignored pattern, so projection covers every non-WFI32-bit word, and the single exact WFI case covers the unique exception. A separate generated classifier uses `noinline,noipa` to retain independent executed selection paths and passes another2,097,185 checks.


## Decision: parked after quiet screening

The timed baseline includes the independently promoted snapshot classifier: core `b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606`. The decoder-only variant over that baseline is `bc18b47fddf00d404d0005d7146e629e9b775ac381c25ee98bd7847820820591`. A fresh100M MBX/software interpreter/seed preflight again passes complete original raw hashes and counters. Both timed cores use the same original seed and identical capture behavior.

Two100M warmups finish before timing, warming both core modules and the shared original seed module. Warmup CPU/counters are retained explicitly. The quiet sequence then restores the same fixture before each100M run:

| Order | Core | Synchronous emulator run time |
|---|---|---:|
| 1 | Baseline | 3,666.136 ms |
| 2 | Decoder | 3,574.121 ms |
| 3 | Decoder | 3,766.938 ms |
| 4 | Baseline | 3,588.738 ms |

The forward result is **+2.510%**, reverse **−4.966%**, combined **−1.188%**. All four full raw checkpoints and original generated retirement/call/region counters match. The complete quiet section takes23.093 seconds including restoration and hashing, which are excluded from the synchronous emulator intervals; separate process CPU values remain in the report. This is a small screening test, not browser navigation timing or evidence of a precise1.188% regression. It shows no repeatable benefit, so **the candidate is parked without production promotion or further timing**.

The first attempted entire ABBA is retained under [contaminated](contaminated/): two brief AMC native relinks overlapped its first arm and were disclosed promptly. No individual samples were selected from that sequence. The entire sequence was repeated after explicit heavy-work completion from the other agents; only that clean repetition informs the decision.

The actual CPU profile identified a real containing function, but reducing one source-level portion of it did not yield a reliable end-to-end execution improvement. The original decoder remains in production. [Full clean results](quiet-report.json) · [decision](decision.json).
