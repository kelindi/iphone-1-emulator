# Actual old-shell to worker-shell offline upgrade

The preserved [24-check report](report.json) passes without uncaught page errors. Unlike a same-source A/B cache test, version A uses the actual previous `app.js` and `storage.js`, and has no compression-worker asset. Version B uses the reviewed worker implementation now in production. The harness adds only bundle markers and a valid identifying WASM custom section. Both versions execute the genuine guest.

The old saved record survives waiting, ordinary activation after all old tabs close, and disconnected loading with **all metadata and compressed bytes unchanged**: 248,983,522 raw bytes, 103,586,987 gzip bytes, SHA-256 `74d615a2e917d98c3f03182a6663fd6e679554a66b5d35466123ecbd23558c16`. The introduced worker is present in the new cache with its correct hash. During a real offline Save, native-worker observations show ready, a single 248,983,522-byte transfer with the sender detached, and the matching encoded response.

That new Save produces 103,586,852 gzip bytes, SHA-256 `bfcc1275def57a984bec608ac3b80b4b6c8c888da8cada001ab1e63c965883ee`, and survives another disconnected reload with exact metadata and bytes. It is a new capture, after the live guest briefly resumed for generated-execution checks; its bytes are not expected to equal the pre-upgrade Save. Disconnection is established by a failed uncached fetch, independently of `navigator.onLine`.

[Before update](settings-before-update.png), [waiting update](update-waiting.png), [after update](settings-after-update.png), and [new offline Save](new-worker-save-offline.png) retain the actual browser screenshots. This is functional upgrade evidence; the visual Save milestone's original-speed recording is linked in the [parent investigation](../README.md).

## Reproduction

Run from the repository root with installed browser dependencies and the matching production firmware/runtime/seed. [Source hashes](source-hashes.json) record all uninstrumented old/new assets, and the two shell folders preserve their text sources. Large WASM files and firmware are deliberately not duplicated. `stage.py` verifies the required current binaries and stages isolated sources; it refuses an existing destination. Use a fresh output directory for every run.

```sh
python3 docs/traces/checkpoint-worker/offline-upgrade/stage.py .context/offline-worker-repro
ARTIFACT_DIR=.context/offline-worker-repro/results SOURCE_WEB_ROOT=.context/offline-worker-repro/new-shell PREVIOUS_SHELL_ROOT=.context/offline-worker-repro/old-shell PORT=8164 node docs/traces/checkpoint-worker/offline-upgrade/verify.mjs
```

The original report names `web` as its old source because the test ran before promotion. The archived old shell preserves those exact bytes. The harness and report are unchanged from that run; staging is a later reproduction helper. The service worker is regenerated for each diagnostic generation from the retained template. No production files are modified by this test.
