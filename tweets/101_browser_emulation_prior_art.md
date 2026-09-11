# Browser performance is still an architecture question

## Tweet idea

We have not proved that a real original iPhone is too demanding for the web.
Research points to richer compilers, longer compiled execution and cheaper
memory/code validation. Failed micro-optimizations don't settle that question.
The next experiment should test an architecture, not another tiny tweak.

## What you're seeing

This is a research update, with no new runtime or performance recording.
[Milestone100's existing recording](videos/core_decoder_comparison.mp4), in its
repeat Settings/Home sections after the first pair, shows the unchanged performance
problem motivating the research. It does not demonstrate any proposed solution.

The [research report](../docs/research/browser-emulation-strategies.md) links primary
project documentation, implementation details, limitations and ranked next steps.

## ELI5 technical explanation

Our accelerator translates small pieces of the old phone's instructions ahead of
time. It still often hands control back to the interpreter. In one recorded metric
window it averages about8.6 guest instructions per generated call, including calls
that make no progress. That suggests investigating handoff costs, though it does
not measure their share of the delay.

Other projects show ways to compile connected code, remember when translated
code becomes invalid and optimize known binaries before users run them. An
experimental QEMU-to-WASM backend offers prior work to evaluate. These projects
run different systems with different accuracy requirements, so their performance
claims are not forecasts for our phone.

Our native comparison also used a native interpreter, not an optimized native
ARM recompiler. Its small warm-use advantage did not prove an emulation speed
ceiling. Earlier decoder/coverage experiments narrowed particular ideas; they did
not test all these larger architectural choices.

## Why it matters

We now have concrete prior art and a proposed feasibility gate before another
large implementation. The most promising direction is keeping real guest work
inside compiled code longer while preserving memory, faults and timing. No
speedup or full-speed guarantee is claimed; production is unchanged.
