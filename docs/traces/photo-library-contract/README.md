# Original camera-roll file discovery

Read-only inspection identifies a smaller first Photos experiment than constructing an iTunes-synced photo database: the original **CameraAlbum** enumerates DCF directories and file groups on disk. This is a source-derived candidate, not a working photo import or Camera claim. No image or production phone was modified and no guest was run.

The exact original PhotoLibrary and UIKit identities are in [provenance.json](provenance.json). [Annotated original instructions](annotated.txt) retain bytes and addresses; annotations resolve simple literal/PC-relative references and are not a complete execution trace.

- `+[DCFDirectory dcimPath]` at `32758da8` calls `_UIMediaFilesDirectory` and appends `DCIM`. The original UIKit function at `324344f4` calls `NSHomeDirectory` and appends `Media` ([path evidence](media-path.json), [instructions](media-path.dis)). Resolve the actual Photos process home directory before choosing the installation path; the static call does not by itself establish `/var/root`.
- `-[CameraAlbum images]` at `32755298` obtains `DCFDirectory.dcfDirectories`. Directory enumeration at `32759b28` uses `NSFileManager.directoryContentsAtPath:`. File-group enumeration at `32759458` does the same within each accepted directory.
- Directory validation passes required length 8, name length 8 and numeric range `{0,3}`. File validation passes required length 12, name length 8 and numeric range `{4,4}`. The [original validator](valid-dcf-name.dis) checks decimal digits in the numeric range and letters/digits/underscore in the remaining base-name portion, with uppercase normalization for lowercase names. `100APPLE/IMG_0001.JPG` is therefore a concrete candidate to verify in the guest, not accepted runtime evidence.
- `DCFFileGroup` initializes its full-size extension to `JPG`; `addExtension:` selects `TIF` when present ([original instructions](add-extension.dis)). Original type hints are `public.jpeg` and `public.tiff`.
- `pathForThumbnailFile` appends `THM`; `pathForLowResolutionFile` appends `LRZ`. `CameraImage.thumbnailImage` asks genuine `UIImage.initWithContentsOfFile:` to decode the THM file. This identifies an image file, not a raw pixel database record. Required thumbnail dimensions, low-resolution encoding and fallback behavior still need direct verification.

The next bounded experiment should first finish the format/path contract, then put a few independently authored, visibly distinguishable JPEGs and their required companion images into a **disposable cleanly stopped base image**, preserving all old files. Cold boot it, open original Photos, and verify real thumbnails, full-size images, next/previous navigation and ordinary autosave/reload. A negative filename/companion case should be distinguished from valid content. Do not edit the ramdisk behind a paused mounted guest, substitute a host photo viewer, or describe imported test images as Camera captures.

Synced albums, pinch/rotation, slideshow, general media import and Camera remain separate gates. This result does not establish that arbitrary JPEGs copied to a directory will work.

## Offline placement refused; original guest writer prepared

The first disposable-copy preparation fails its catalog-capacity check before
publishing an image. The source remains unchanged. Three new directories fit,
but a file insertion needs 4,194 bytes in a 4,096-byte leaf. A read-only planner
then tries all 24 permutations of the three unused directory CNID holes plus a
fresh ID; even the best placement overflows a later file-thread leaf at 4,116
bytes. No node split, original-record removal or relaxed capacity check was used.
Private preparation failures and the planner remain under `.context/photo-fixture/`.

The successor is an [own one-shot guest importer](../../../tools/legacy_services/photo_bootstrap/README.md).
Only its executable and launchd plist are installed offline on a copy; original
`mkdir/open/write/fsync/link` operations can then let the guest's real HFS driver
manage its catalog. The [native importer qualification](../photo-bootstrap/README.md)
now verifies all five actual guest readbacks and backing-file payloads. A separately
bounded continuation reaches Home after the first4B Home budget proves insufficient.
Original Photos display/persistence remain untested. The five independent JPEG/THM
assets include two distinct valid candidates and the invalid `BAD.JPG` control.
