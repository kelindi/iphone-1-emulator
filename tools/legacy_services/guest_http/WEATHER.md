# Weather demo body module (not guest-qualified)

`weather.c` builds deterministic XML response bodies for the original OS1
Weather gateway. It uses the established dataset and response structure from
`../weather.py` and `tools/legacy_services/fixtures/weather-demo-backend/`. It introduces no
libc imports and can be linked into the independently authored ARMv6 daemon.

The API expects validated, decoded request fields. `own_weather_forecast` takes
1–16 identifiers (each at most128 bytes), recognizes the exact original
Cupertino/New York IDs, preserves request order and removes duplicates.
`own_weather_search` accepts at most128 ASCII bytes, normalizes case/whitespace,
and returns complete matching location records. Unsupported queries yield empty
lists; malformed/bounded-output failures return0 with length0. Output has an
explicit length, not a trailing NUL. Never send the buffer after return0.

The XML carries demo metadata; this does not replace visible product labeling.
Six fixed forecasts are included per recognized city. Numeric day/night values
and code32 retain the standalone backend's stated uncertainty: actual original
parser/display behavior is not established.

Run the freestanding host contract checks:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  tools/legacy_services/guest_http/weather.c \
  tools/legacy_services/guest_http/test_weather.c -o /tmp/weather-body-test
/tmp/weather-body-test
```

## Required routing and acceptance

Original endpoint: HTTP port80 on `iphone-wu.apple.com`, POST `/dgw` with query
parameters `imei`, `apptype=weather`, `t`, and UTF-8 XML Content-Type `text/xml`.
The maintained Safari daemon currently uses127.0.0.1:18081 and stops reading at
the header terminator; it cannot presently serve this request correctly.

A bounded isolated-image experiment can add a hostname override to `/etc/hosts`
and an independently authored root listener on127.0.0.1:80. The resolver's actual
use of this file, application reachability checks, and absence of a port conflict
must be checked in the guest. This is an ordinary configuration experiment, not
an Apple executable patch. Preserve original fork hashes except explicitly
listed own files/configuration. Do not mutate an existing mounted production
image or present guessed routing as proven.

The transport must collect exactly a bounded Content-Length body across partial
reads and validate the original request envelope/query before calling this module.
Do not infer query type or identifiers from substring matches in arbitrary XML.
Keep raw bounded request/response evidence, without publishing device identifiers.

First prove hostname resolution and a real port80 request from original Weather.
Then prove forecast rendering, Fahrenheit/Celsius, search/add/remove, exact
Save/reload persistence and unavailable/unknown-city behavior. Record genuine
app output and show visible demo labeling before promotion. No boot or guest
network experiment has been run for this module yet.
