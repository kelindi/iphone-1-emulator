# Bounded original Weather gateway parser

`own_weather_request(body, bytes, output, capacity, &length)` now bridges a complete
HTTP request body to the Weather demo builders. The transport supplies exactly
the declared body bytes, with a maximum of16384; return0 is malformed/unsupported
input or insufficient response capacity, and always sets output length0.

Supported original shapes:

- `request api="weather" apiver="1.0.0"`, with exactly one `query`.
- Forecast query `id="30" type="getforecastbylocationid"`: ordered `list` of
  1–16 `id` text elements, `language` equal to`en`, and `unit` equal to`f`.
- Search query `id="3" type="getlocationid"`: one `phrase` text element.
- Optional XML1.0 declaration with UTF-8 encoding; single/double attribute quotes,
  whitespace and decoded ASCII numeric/named XML entity references.

This is a bounded protocol dialect, not a general XML implementation. Comments,
DTDs, external/entity declarations, namespaces in element names, self-closing
forms, non-ASCII decoded text and extra child elements are unsupported. Duplicate
attributes, mismatched closes, malformed entities, trailing documents, oversized
values and truncated input are rejected. Decoded entity text is never reparsed
as markup; only known fixed city data is emitted. Forecast input remains F as in
the original request; the original app performs its own Celsius presentation.

Five Python tests compile the actual C implementation and compare response trees
against the independent existing Python backend using retained original-shape
forecast/search fixtures. They also cover entity decoding, malformed forms,
every truncated forecast prefix, and exact output capacity. Run:

```sh
python3 tools/legacy_services/guest_http/test_weather_request.py
```

## Installer/routing constraint

The current `install.py` deliberately adds exactly two own files and asserts
that every existing catalog record and file fork remains unchanged. It cannot
modify an existing hosts file; silently weakening its whole-image audit would
invalidate its established guarantee.

For the isolated Weather route, first inventory the actual source image for
`/etc` and `/private/etc/hosts` (resolve the existing symlink/real target). Add a
separate opt-in installation mode or own utility which accepts an exact original
hosts hash, preserves all prior bytes, and appends only a clearly marked mapping
`127.0.0.1 iphone-wu.apple.com`. Audit that single configuration fork against its
explicit expected replacement while retaining exact preservation for every
other original fork, especially Apple executables. Record the before/after
configuration hashes and never claim all original forks unchanged for this mode.
Perform edits on a new unmounted copy only. If hosts is absent, adding it is still
an explicit additional-file policy change, not something the two-file installer
currently permits.

The service must listen on loopback port80, collect bounded full POST bodies,
and route only the known Weather gateway path. Actual OS1 resolver behavior,
reachability, request delivery and parser/display acceptance still require a
coordinated guest experiment. No image or guest binary has been modified here.
