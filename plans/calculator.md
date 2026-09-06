# Genuine Calculator workflow acceptance

The production browser’s original iPhone OS 1.0 Calculator passes twelve arithmetic workflows in a fresh disposable profile using accelerated MBX graphics and the guarded CPU engine. The original software-renderer acceptance established the reviewed references with exact hashes; the current MBX revalidation retains those references with the narrow rounding allowance described below.

| Workflow | Real key sequence | Displayed result |
|---|---|---|
| Clear entered digits | 123, C | 0 |
| Addition | 12 + 34 = | 46 |
| Negative subtraction | 9 − 14 = | −5 |
| Multiplication | 7 × 8 = | 56 |
| Division | 81 ÷ 9 = | 9 |
| Fractional division | 1 ÷ 4 = | 0.25 |
| Decimal entry/addition | 1.5 + 2.25 = | 3.75 |
| Decimal quotient | 10 ÷ 4 = | 2.5 |
| Negative result back to positive | 0 − 7 =, + 10 = | 3 |
| Repeated digit entry | 111 + 222 = | 333 |
| Continue from a result | 2 + 3 =, × 4 = | 20 |
| Clear and reuse | 98765, C, 6 × 7 = | 42 |

Every calculation starts with the genuine C key, apart from the first clear probe. The recorded OS1 keypad offers C and memory keys but no visible sign-toggle or backspace button. Negative signs are verified through arithmetic. Hidden correction gestures, memory operations, repeated-equals semantics, exceptional arithmetic, digit limits and saved Calculator state remain untested.

## Maintained regression

Run `npm run test:calculator -- --record` after starting the built production browser. `BASE_URL` and `ARTIFACT_DIR` select the server and disposable output directory. The harness uses real mouse touches on the guest canvas, then compares every pixel of the native **320×88 RGBA8 display area at (0,20)** against the visually approved reference PNGs and their verified hashes in `web/tests/include/calculator-displays/` and `calculator-displays.json`. The crop excludes the changing status clock, physical frame and button highlights. It checks the rendered number; a click or merely changed pixels is not a pass.

`--capture` supports discovery: it writes screenshots and expected expressions but deliberately never sets acceptance `passed=true`. Updating references requires visually checking the actual numbers again; the harness does not derive reference glyphs from key coordinates. The twelve approved discovery screenshots were all inspected before the reference file was created.

The historical software-graphics verified report is `.context/calculator-production-verified/report.json`; original discovery evidence is `.context/calculator-discovery/report.json`. All twelve pixel comparisons and the positive generated-instruction gate pass. Fixed waits of 50 ms held touch, 500 ms between keys and 1.3 seconds before result capture establish functionality, not a speed benchmark or minimum input latency. No production core, firmware, app binary or framebuffer behavior changed for this work.

[Milestone 048](../tweets/048_calculator_workflows.md) preserves the complete original-speed acceptance recording and an actual decimal-result thumbnail.


## Accelerated default and short-tap revalidation

The rebuilt production core `603679fb…c84f` passes all twelve cases after correcting the missing held-contact report on quick first taps. The first fixed-delay attempt captured a genuine unfinished blank display; a separate no-input timeline reproduced that frame and naturally reached Calculator zero. The maintained harness keeps its immediate 50 ms tap and waits at most 30 seconds for two consecutive matches to the initial display. All opening samples are retained. This is a bounded functionality gate, not a faster-launch claim.

The original reference PNGs and SHA256 values remain unchanged. Accelerated composition differs slightly at glyph edges: independent decoding of all 337,920 result pixels found only two pixels with a two-level green-channel difference; all other changed channels differ by one and alpha matches exactly. The maintained comparison therefore requires **maximum RGB difference ≤2 and exact alpha**, without an average-error or changed-percentage allowance. All 132 comparisons to other answers fail that threshold (minimum maximum difference 166). Each actual number was also read independently.

The final browser replay passes all twelve cases and the positive compiled-execution gate; every result hash matches the earlier recorded run. [Milestone 063](../tweets/063_short_tap_contact_report.md) links the actual production recording and thumbnail, while [durable verification](../docs/traces/short-tap/production/verification.json) retains both failed test attempts, the independent PNG review, negative comparisons and final report. Software exactness and MBX bounded color equivalence are distinct claims.
