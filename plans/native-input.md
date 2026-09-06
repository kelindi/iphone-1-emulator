# Native explicit touch release

The native `iphone1 --at STEP touch up X Y` command now releases the original iPhone's contact correctly. This fixes the harness dispatcher, with no browser, controller-model, snapshot-format, or Apple executable changes.

## Cause and protocol boundary

The parser records `touch up` as an explicit `BREAK_TOUCH` contact. The Z1 controller API takes the complete active contact set and derives its own MAKE/TOUCHING/BREAK phases. Passing a one-contact BREAK therefore either failed behind an unread frame or normalized back to a held contact. The native harness logged the event but never issued a real lift.

The dispatcher now sends the empty active set when the machine has Z1 and the requested phase is BREAK. Z1 retains any unread MAKE/MOVE, then generates its BREAK after consumption. The N82/Z2 path keeps the original explicit one-contact BREAK packet; its current encoder rejects an empty contact set. This distinction is why changing the parser to universally use `INPUT_LIFT` would regress N82.

## Verified sequence

Using the genuine, USB-enabled OS1 Repair-alert checkpoint at 10B retired instructions:

```sh
build/tools/iphone1 --board m68 --ram-mb 256 \
  --kernel web/firmware/kernel_1A543a.macho \
  --devicetree web/firmware/devicetree_m68ap.bin \
  --load .context/lockdown-root/usb-matched-home.snap \
  --steps 10060000000 \
  --at 10010000000 touch down 160 300 \
  --at 10030000000 touch up 160 300 \
  --pc-log 0x313097a4
```

The original userspace multitouch trajectory parser receives 38 contact reports: one MAKE (phase 3), 36 TOUCHING (phase 4), and one BREAK (phase 5). The BREAK arrives at instruction 10,030,057,505, or 57,505 instructions after the explicit up command, with zero pressure/density bytes. The kernel performs the final real SPI result-length/result-data reads. Continuing the same replay to 10.5B instructions dismisses the alert and reaches Home.

The cited checkpoint is workspace evidence, not a new required tracked firmware fixture. Logs and the summarized driver report are retained in `.context/native-touch-release/`. The maintained regression below needs no firmware.

## Maintained regression

```sh
cmake -S . -B build
cmake --build build --target iphone1 test_iphone1_input
ctest --test-dir build -R '^test_iphone1_input$' --output-on-failure
```

The 51-check test includes the actual harness dispatch and exercises the real M68/Z1 and N82/Z2 controller models. It consumes reports over their SPI endpoints, checks that unread MAKE and MOVE frames survive an early up, verifies the later BREAK and absence of recurring held frames, checks idempotent Z1 release, and checks the N82 MAKE/TOUCHING/BREAK payloads. Replacing only the dispatch fix with its previous behavior causes this regression to fail at the first early-up acceptance check.

This is a correction to explicit native touch commands. It does not establish broader gesture coverage or change other pre-existing harness input policies.
