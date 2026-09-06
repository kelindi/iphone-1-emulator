# The real iPhone OS exchanges a local web page

## Tweet idea

The original iPhone OS just carried a real HTTP exchange through its own TCP stack. A tiny program inside the phone connected to its own local server, sent a request and received every response byte. Next: open that local service in the original Safari.

## What you're seeing

This is a nonvisual networking milestone, documented by the [actual guest console](../docs/traces/guest-loopback/observer-v2/run-1/stdout.log): `OWN entry`, `bind`, `connect_status`, `actual_request`, `actual_response`, `OWN full_text` and `OWN exit_entry`. [Independent native/UART audit](../docs/traces/guest-loopback/observer-v2/run-1/audit.json) verifies the matching observations. No Safari screen or recording is claimed.

## ELI5 technical explanation

A phone can talk to itself through a local network address, even without internet service. Our own small program used Apple's original socket functions to do exactly that: listen, connect, accept, send, read and close. The54-byte request and113-byte response match exactly, including the29-byte body and required end of stream. Existing Apple files stayed unchanged.

Our first measuring tool checked too much code before all its pages needed to be loaded. It missed its identity check, so that run remains incomplete. The corrected tool checks the active code page first and all code later, without changing the guest's memory or CPU state. The [full evidence](../docs/traces/guest-loopback/README.md) retains both attempts and the tests.

## Why it matters

This gives us a real local route for testing demo services with the original apps. It does not yet prove Safari rendering, Weather forecasts or internet access. Those app workflows come next; production is unchanged.
