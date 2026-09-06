# Releasing a native test finger correctly

## Tweet idea

A tiny test-harness bug made our emulated finger stay down after “touch up.” The original iPhone's sensor expects the list of fingers still touching, not a release flag attached to a finger. We now send the empty list and let the real driver receive its release.

## What you're seeing

This is a nonvisual input-protocol milestone. The matching evidence is the original guest driver trace in `.context/native-touch-release/trajectory.log` and its `driver-report.json`, specifically the report at instruction **10,030,057,505**, following the explicit up command at 10,030,000,000. No screen-speed or new browser-gesture claim is made. [Reproduction and maintained regression](../plans/native-input.md).

## ELI5 technical explanation

The native command-line harness used to pass a finger marked BREAK to the Z1 sensor. That sensor interface treats every supplied finger as still touching and constructs its own phase reports. The finger therefore stayed present. The browser already used the correct empty contact list for release.

The native dispatcher now sends that empty list for Z1 release. An unread press or move remains intact until the guest consumes it; the sensor then produces the real release. The later N82/Z2 model still receives its explicit BREAK record because it uses a different protocol.

The genuine OS 1.0 userspace driver receives 38 reports in the verified sequence: one press, 36 held reports, and one release with zero pressure. The release follows the command by 57,505 retired instructions. A maintained 51-check regression exercises the actual native dispatcher and SPI endpoints on both models, including release behind an unread press or move. The original dispatch fails the regression.

## Why it matters

Native gesture replays can now deliver the intended release, making hardware and graphics investigations more reliable. No Apple executable, browser input path, sensor model, or snapshot format changes.
