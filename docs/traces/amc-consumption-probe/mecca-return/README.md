# Successful kernel return; userspace startup still unobserved

One fresh observation-only replay keeps all three Stage M input admissions, the fourth-request stop and device behavior unchanged. It records the original configuration dispatch return at **c01533c2**, Thumb/SVC, with **R0 = 0**. That instruction actually retires to c01533c4 at cycle 9,873,234,593. Its mapped 32-byte original kernel code window matches independently pinned source bytes.

None of the four selected MeCCA user return/call sites or kernel explicit-start entry was observed before the same fourth-request stop. Therefore no user scalar-output count, user-level error or explicit-start call is reported. In particular, successful mapping/code checks apply to the one observed kernel site; absence of a user-site hit does not itself establish that the proposed userspace VAs were mapped in this address space. The caller may not yet have returned there, or its actual mapped return location may need resolution. This is an unresolved observation, not a diagnosed decoder or scheduler failure.

All 15 final state artifacts and the complete trace after removing only ORDER lines are byte-identical to Stage M. The entire original program, first075 PCM/counters, three actual input spans and owned queue pass their independent audits. There are still zero output reads, zero PCM coverage, no output acknowledgement or ownership return, and one unchanged PCM publication. No fourth input, new device transition or playback occurred.

## Evidence

- [Single-run report](run.json): one 5.09-second functional run, exit 10 at the planned fourth-request cap, no retry. A concurrent root WASM build means this is not a timing comparison.
- [Actual return and exact-state audit](ordering-audit.json), [full trace](trace.log), [original-file code-window oracle](original-context-oracle.json), [first075 proof](first/functional-audit.json), [original program proof](first/upload/kernel-oracle.json), [third input](third-request-audit.json) and [complete queue](queue-audit.json).
- Four actual artifact mutations reject through the same frozen auditors: [input](negative-third-input.log), [queue](negative-queue.log), [register record](negative-record.log) and [mapped code](negative-mapped-code.log). The consumption-only controls remain unrun because no consumption occurred.
- [Source/launcher/reference pins](review-pins.json), [build manifest](build.json) and [isolated observer diff](source.patch).

The observer verifies actual mapped instruction context and ISA at every hit. It records all 16 pre-step registers and actual post-step PC/CPSR/LR/cycles, bounded real stack/object data when available, and true retirement versus interrupt diversion. It never injects return values or supplies engine input. 140 focused native checks and 53 auditor checks pass, including wrong adjacent code, unmapped code, Thumb/mode, branch/LR, IRQ diversion, pointer bounds and preserved returned errors/count mismatches. All retained DMA/read/ownership checks pass.

Next: resolve the actual userspace return mapping/context before interpreting its absence or changing input completion. The original MeCCA startup source remains documented [separately](../third-admitted/startup-source/README.md). No production, snapshot, browser or audio-output change. Original media/PCM and private runtime binaries remain local.
