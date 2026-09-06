# Twelve workflows in the original Calculator

## Tweet idea

Our original iPhone Calculator now has a real-app regression: twelve workflows covering arithmetic, negative numbers, decimals, clearing and repeated input. We tap the 2007 app and check its actual displayed results. All calculations run inside genuine iPhone OS 1.0.

## What you’re seeing

[Original-speed recording](videos/calculator_workflows.mp4): approximately **0:15–0:34** covers clearing, addition, negative subtraction and multiplication; **0:34–0:55** covers division and decimal arithmetic; **0:55–1:22** covers returning from negative to positive, repeated digits, chained operations and clearing/reuse. The [thumbnail](thumbnails/calculator_workflows.jpg) is the actual display of **3.75** after **1.5 + 2.25**. Startup and fixed input waits are retained; no animation is sped up.

## ELI5 technical explanation

Clicking a button does not prove that the old phone processed it correctly. We first photographed each genuine result and read the number. Those reviewed display images became references for a second fresh browser run. The maintained test taps the actual app again and compares every pixel in its number display, excluding the changing clock and button highlights.

All twelve results match: 0, 46, −5, 56, 9, 0.25, 3.75, 2.5, 3, 333, 20 and 42. The compiled CPU engine is enabled and retires real guest instructions during the run. The surrounding web page does not perform the arithmetic or generate the displayed digits.

## Why it matters

Calculator moves beyond a single addition demo to a repeatable offline app workflow. Negative results, decimals and continued entry now have concrete coverage. Memory keys, hidden correction gestures, exceptional arithmetic and saved Calculator state remain separate tests; the original keypad has no visible sign-toggle or backspace control. Fixed waits make this a functionality gate, not a speed claim. [Reproduction and exact cases](../plans/calculator.md).
