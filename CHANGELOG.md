# Changelog

## v1.0.0 — 2026-11-14

First versioned release of the wired CarPlay receiver for M5Stack Tab5 / ESP32-P4 v1.x.

- Video/touch, music/navigation audio mixing, microphone, persistent volume/brightness and battery charging.
- Waiting-screen Clear pairing with confirmation, disconnected-session checks and namespace-scoped erasure.
- Navigation stream ID recycling; USB channel error handling, TX/RX watchdogs, power-cycle recovery and task progress guard.
- SD runtime/crash logging and version identification.
- Removed firmware demos, boot audio tests, maintenance reset mode and one-off source injection scripts; retained regression tests.
- Added isolated release builds, app/full BINs, configuration and SHA-256 manifests.

Known: switching to iPhone 17 Pro / iOS 26.5 may require restarting Tab5 after Clear pairing. Experimental authentication identity is not part of the public source; authenticated binaries remain local. No-auth builds cannot start CarPlay.

Details and validation boundaries: [v1.0.0 release notes](docs/releases/v1.0.0.md).
