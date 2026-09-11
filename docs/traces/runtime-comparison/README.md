# Does native execution justify moving the phone onto a server?

**Decision: keep local browser execution for now.** The current native engine
is faster on this Vercel Sandbox, especially on first launch, but the repeat-use
advantage is small enough that streaming can consume it. This is a bounded pilot,
not a completed remote-streaming implementation or a reference-laptop benchmark.

## Actual results

Same Intel Xeon2.90GHz VM,8 logical CPUs, same shipped Home snapshot and current
graphics model. Order: native / Chrome / Chrome / native. Each fresh phone runs
four Settings/Home pairs. There are32 actual actions; six warm samples per action
and execution option. All32 final frames show the intended original app contents.

| Release-to-ready | Browser WASM | Native engine, before streaming |
| --- | ---: | ---: |
| First Settings, median of2 | 3.563s | 2.270s |
| Warm Settings, median of6 | 2.243s | 2.045s |
| Warm Home, median of6 | 0.842s | 0.806s |

The repeat Settings advantage is198.3ms/8.84%; Home is36.1ms/4.29%. Adding more
than those amounts in total streaming latency erases the corresponding advantage.
Those thresholds include transport, encode/decode, buffering and presentation;
they are not network-RTT measurements. First Settings gains1.294s before streaming.

Do not compare this pilot's2.24s warm result against the historical3–3.6s reports
as a code improvement. This VM reports a2.90GHz CPU; earlier sessions reported
2.50GHz, and workload/sample methods differ. No production optimization was made.

## Method and limitations

`native-navigation.c` uses the actual maintained browser C API, input queues,
monotonic host-clock policy and direct RAM writes, linked to current native
GCC-O3/LTO core. The available x86 native engine is an interpreter. Chrome uses
the complete current web package with its existing compiled WASM seed.

Both arms use30ms presses,50ms pixel sampling,500ms stability confirmation and
one-second guest-running gaps. Readiness is reported from the start of the stable
interval. The predicate matches maintained `web/tests/performance.mjs`; every
settled screenshot was additionally reviewed. This does not establish identical
whole guest states across native and WASM, nor eliminate the known WASM VFP issue.

Browser includes worker/canvas presentation and recording. Native samples direct
scanout and writes its screenshot after measuring the action. Thus native timings
are engine-side headroom, not a complete native desktop UI or hosted session.
Browser autosave remains enabled; native has no automatic host persistence.
The comparison does not isolate either difference or establish tail latency.
First launch uses a fresh browser profile; it is not the cached-return-visit test.

## Encoding feasibility

`encoding.json` records three independent H264 encodes of600 frames from actual
browser motion, cropped to320×480 and resampled from the recording's25fps to30fps.
This does not claim30 distinct guest frames. x264 uses one encoder thread,
ultrafast/zerolatency, CRF23, no B-frames and a30-frame keyframe interval.

- CPU cost:0.894–0.899ms per output frame; approximately2.7% of a core at30fps.
- Output:1,382,180bytes for20seconds, approximately0.553Mbps for this clip.
- The encoded stream fully decodes without errors.

These are throughput and CPU-cost measurements. They exclude concurrent emulator
contention, per-frame delivery latency, client decode, internet transport and
streaming protocol overhead. Actual user-network latency remains unknown.

## Artifacts and reproduction

`summary.json` contains every timing and the break-even calculation. Individual
reports, all native RGBA captures, browser PNGs, both original browser WebMs,
review sheets, run controller, source pins and encoding commands are retained.
`archive.json` hashes the initial retained experiment artifacts; `sources/` also
preserves the measured browser harness/API/server. `host.txt` records the host.
`media.json` pins the milestone MP4 and thumbnail. All scoped benchmark processes
finished, and the local server closed. No new hosted service was deployed.

The temporary raw Home fixture was removed after checking its full hash; recreate
it by concatenating and gunzipping the two shipped snapshot parts. Expected raw
size244,887,522; SHA256
`764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244`.
Copy this directory's native source and controller into `.context/runtime-comparison`
to retain their documented relative paths. The native compile command is:

```sh
cc -O3 -DNDEBUG -flto -Icore/include -Iweb/tests/include \
  .context/runtime-comparison/native-navigation.c \
  .context/mbx-stride-integration/native-build/core/libemucore.a \
  -lm -o .context/runtime-comparison/native-navigation
```

Freshly build the current Release emucore into an isolated directory if the
historical local archive is unavailable, and record the new compiler/build pins.
Run contracts and limits are in `contract.md`; do not overwrite existing outputs.

[Milestone and complete browser-arm recording](../../../tweets/098_native_browser_performance.md).
