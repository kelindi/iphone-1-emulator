# Updating the offline phone without losing its save

## Tweet idea

An installed web iPhone now checks for updates when it opens. The update waits while your phone runs: Save, close every phone tab, and reopen. Chrome verified that the new build restores the exact same saved Settings session—even with the network then disconnected.

## What you're seeing

[Recording](videos/offline_bundle_upgrade.mp4) · [thumbnail](thumbnails/offline_bundle_upgrade.jpg).

- **0:00–0:31.84:** version A installs offline support, opens genuine Settings, saves, then discovers version B waiting after a reload. The update notice appears below the phone.
- **0:31.84–0:36.20:** a second phone tab still uses A. One remaining tab is enough to keep the update waiting.
- **0:36.20–0:43.24:** after both tabs close, reopening uses B and restores Settings. A further disconnected reload also restores the phone.

These are three complete tab recordings joined at original speed, with explanatory labels added. The second tab overlaps the first briefly in the original session; this is a sequence of tab views, not an uninterrupted wall-clock recording. The test-only A/B markers and inert WASM custom section distinguish bundle bytes; the guest itself is unchanged. Evidence and limitations are in the [upgrade report](../plans/offline-updates.md).

## ELI5 technical explanation

The browser stores the website around the phone as well as the phone's saved memory and disk. An update must replace the website files together so that its JavaScript talks to the correct emulator module. It must also leave the saved phone alone.

The app now asks an already installed service worker to check for an update. A separate notice explains when it is ready. We let the browser wait until every old phone tab closes before activating the new bundle; we never force it over a running guest. People who have not chosen Keep offline are not automatically enrolled.

Sixteen actual Chrome checks passed. A, B and their WASM hashes stayed consistent across the transition. The saved checkpoint retained exactly the same compressed bytes and metadata, and all 153,600 displayed Settings pixels matched after reopening.

## Why it matters

Offline users can receive future fixes without having their running phone replaced underneath them or their saved data reset. An older shell still needs normal browser update discovery or another Keep offline click to acquire this new check. Changing the default starting firmware remains separate: an existing saved phone keeps its old boot state until the user explicitly resets it after exporting a backup.
