# Own test photos written by the original guest filesystem

The independently authored importer runs inside original OS1 and creates all
five fixed JPEG/THM files through libSystem. Its readbacks and an independent
snapshot/backing-file audit match every full payload SHA. A separate continuation
reaches genuine Home. The first browser workflow recognizes two photos and renders
the first full-size image, then fails at image navigation. **Rendering and persistence
qualification remain incomplete; no production phone was changed.**

The source contract is [original PhotoLibrary discovery](../photo-library-contract/README.md).
Two valid image/thumbnail pairs use `IMG_0001` and `IMG_0002`; `BAD.JPG` is the
invalid DCF name control. The blue/yellow circle has one white marker, the red/mint
square two. Full images are640×480 and THM files64×48. These are independently
rendered test patterns, not Camera captures. [Generator and sources](source/),
[exact reproduction](pattern-reproduction.json), [nineteen host filesystem
contracts](host-checks-1.json), and [classic ARMv6 build/import audit](source/build.json).
The50,072-byte own executable binds twelve original libSystem exports eagerly;
no Apple code is patched or redistributed here.

## Why the guest writes the files

The leaf-only offline HFS writer refuses five files plus three directories:
its first overflowing leaf requires4,194/4,096 bytes. All24 read-only CNID-hole
placements also fail; the best later file-thread leaf needs4,116 bytes. The
[original refusals](placement/) remain intact. Instead, a new pristine MBX image
copy receives only our program and launchd plist, then the original guest HFS
implementation creates the directories and files itself.

Even the first two-file bootstrap names need4,102 bytes in one leaf. Shortening
**only our new filenames** to `iphone1-photos` and `com.iphone1.photos.plist`
fits4,066 bytes, with no index-first-key change or relaxed capacity guard.
[Both admissions](placement.json), [final installation audit](installed-image.json).
Every original5,120 ordinary fork and5,828 catalog row remains exact, except the
two necessary parent valence increments. Source root73d0… is the current pristine
MBX generation; final copy SHA isb978ba952c953811840aaeb977c2db2a30d660e470d8cd7938afb64497d3a381.

The importer exclusively creates temporary files, writes/fsyncs/closes them,
then uses original `link` to publish without replacing existing content and
unlinks its own temp. Matching prior files require full readback+EOF; foreign
content, symlinks, collisions and I/O errors stop safely. Earlier completed
files can remain if a later file fails. Host checks exercise real disposable
files, partial I/O, EINTR, non-root refusal, competing creators, exact idempotence,
FIFO/symlink rejection and cleanup. They do not substitute for original-OS execution.

## Native result and preserved incomplete first Home gate

The first native boot stops normally at its4B cap in136.52seconds. Actual UART
records all five length/FNV readbacks and `PHOTO_COMPLETE_V1` by259.59M instructions.
Its final frame is still original kernel boot text, so **its Home gate fails**.
The original automatic `run.json` pass applies only to its program/step checks;
it did not implement the separately required manual Home acceptance.
[First run and frame](native-run-1/), [first artifact audit](native-run-1/artifact-audit.json).

The initial backing-file auditor also mistakes HFS hard-link directory records
for ordinary payload records. They have empty data forks. Actual Finder
`hlnk`/`hfs+` signatures and link references lead to the five private `iNode…`
files, exactly as described by [Apple TN1150, Hard Links](https://developer.apple.com/library/archive/technotes/tn/tn1150.html#HardLinks).
A separate read-only correction verifies those actual targets and rejects seven
mutated metadata/digest controls. The first failure remains preserved; no guest
was repeated to fix this auditor. [Corrected artifact audit](native-run-1/artifact-audit-corrected.json).

The original native loader accepts the standard snapshot, and an independent
RAM-section decoder compares all256MiB against the actual final RAM dump. The
complete process list contains SpringBoardPID12/fourthreads and lockdowndPID17/
threethreads; availability is explicit and the loaded CPU stays byte-identical.
This is process presence, not a scheduler or service-response test.

Historical pristine-MBX boot evidence and the independent current service boot
put Home around8.5–10B, unlike the earlier retained-data qualification image.
The [separately frozen continuation](continuation-contract.md) therefore resumes
the exact saved4B state once, to10B absolute, with no reinstall, new cold boot or
input. It closes normally in223.75seconds. [Actual Home+Repair frame and acceptance](native-continuation-1/home-acceptance.json)
and [full RAM/backing-file revalidation](native-continuation-1/artifact-audit-corrected.json)
pass. Both runs overlapped other functional native work; neither duration is a
speed benchmark. The original4B failure is not converted into a successful
uninterrupted boot gate.

All new installed-image, RAM and snapshot files are [losslessly archived with
full raw SHA/size receipts](archived-native-artifacts.json), reclaiming677,476,747
bytes. Their original raw paths are now absent. Restore any through the existing
[materializer](../storage-reclamation/materialize.py) and adjacent `.archived.json`
receipt before reproducing scripts that expect raw input. Current default assets,
old browser saves and canonical replay fixtures remain intact.

## First original Photos browser workflow — failed

One313.486-second session imports the qualified native Home into the unchanged
current MBX browser core. Original Photos displays Camera Roll(2), both correct
thumbnails, and no invalid-name control. The first circle image eventually
renders full-size. Showing the controls reintroduces stale album content, and
next-image attempts leave a narrow blue strip instead of the square image.
The session stops at the unfinished `square-fullscreen` gate with no retry.
[Milestone094 and complete recording](../../../tweets/094_photos_first_workflow_failure.md),
[original report](browser-run-1/report.json), [independent artifact audit and limits](browser-run-1/acceptance-audit.json).

All32 received local responses match the pinned assets, all16 projected source
files remain unchanged, and all12 captured320×480 RGBA buffers pass full hashes.
Browser and server close normally. These checks establish provenance, not a
successful Photos workflow. Second image, previous/next, Home/revisit and actual
autosave/reload gates remain incomplete. Background saves are not persistence
acceptance. This was an adaptive functional run with explicit waits and human
review; it is not a navigation benchmark.

During the first transition a live metrics inspection reports a 3D-blit graphics
restart. The controller did not retain a complete serial stream or export the
failed guest state, so this observation does not establish the cause or a shared
cause with Safari's separately identified2D source-stride rejection. All action
times, screens, native input and the full browser recording remain available.
The first Next tap may follow control auto-hide; a second center tap followed
one second later by Next also fails to display the correct second image.

Camera, synced albums, arbitrary upload, slideshow, zoom/rotation, continuous
navigation and default distribution remain separate work. The next step is
correcting and qualifying actual graphics behavior before extending this fixture.
