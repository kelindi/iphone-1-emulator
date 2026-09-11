# 109 — The first compiler slice executes real WASM

## Tweet idea

Our iPhone compiler now produces executable WebAssembly. Its first integer and
branch slice passes 4,098 checks against the existing CPU interpreter, including
flags, backward loops and exact stopping points. Next we connect it safely to
the phone's scheduler.

## What you're seeing

This is a nonvisual compiler-execution milestone. The Settings/Home section of
the [106 baseline recording](videos/adaptive_compiler_baseline.mp4) shows the
unchanged phone for context; it does not run the new emitter. [Execution evidence](../docs/traces/adaptive-emitter/README.md)
contains the actual WASM comparison results. No new UI or speed recording is
claimed because the compiler is not connected to the browser phone yet.

## ELI5 technical explanation

The earlier decoder understood a small set of iPhone instructions, and the graph
builder connected their possible routes. The emitter now turns those descriptions
into code the browser's WebAssembly engine can execute. Registers stay in local
variables across connected instructions, then are written back when execution
stops.

Stopping correctly is as important as computing correctly. If code changed, an
instruction is unsupported, or the scheduler budget ends, the emitter reports
exactly where it stopped and how many instructions completed. It cannot execute
half an instruction and ask the interpreter to repeat its effects. Tests exercise
arithmetic extremes, condition flags, branches, loops and these stopping rules.

## Why it matters

This is executable compiler code, with 4,098 CPU comparisons and 95 unit tests
passing. It is still an isolated integer/branch slice. Runtime admission, real
memory guards, device timing, invalidation and the full performance gate remain.
The phone has not become faster from this milestone yet.
