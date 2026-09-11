# 125 — Safari form works; original OS identifies service crash

## Tweet idea

Original Safari now submits our local form with the current renderer. The next page still fails—but this time we recovered the cause of the missing server: the original OS recorded SIGBUS, a memory-access signal. We can distinguish a service crash from a dead internet connection.

## What you're seeing

[Recording](videos/safari_service_sigbus.mp4) · [Thumbnail](thumbnails/safari_service_sigbus.jpg). Around05:55–06:15 the original Safari form displays `Message: go`; subsequent navigation shows lost connection and reload cannot find the server. The full recording retains the failed workflow. This is an isolated imported phone, not default-product acceptance or a speed benchmark.

## ELI5 technical explanation

Safari talks to our small program through the original OS's real local network. A successful form response proves that path works, but later the server disappears. We read the original launch manager's saved job record without running or changing the phone and found its recorded termination signal: SIGBUS10. That means the process suffered a memory-access fault. It does not reveal which instruction caused it. The available crash report is for Apple's crash reporter itself, so its address cannot be blamed on our server.

## Why it matters

We now have a specific service-lifetime failure to investigate, independent of compiler speed. Supported launchd recovery and persistent own logs are being prepared alongside a combined Safari/Weather gateway; recovery will not be represented as a root-cause fix. [Evidence](../docs/traces/safari-service-sigbus/README.md). Weather and Stocks responses remain unverified in their original apps, and default distribution is not complete.
