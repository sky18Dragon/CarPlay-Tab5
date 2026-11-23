# Changelog

## Unreleased

- Pairing storage: confirming Trust no longer rewrites the 6.8 KiB identity record. The trust state is a separate one-entry key, so a second iPhone can pair while the first phone's record is still stored (v1.0.0 stopped at "Couldn't save phone pairing").
- When a new phone's record still does not fit in the 24 KiB NVS, records of other phones are removed and the write is retried once. Those phones confirm Trust again on their next connection.
- Records written by v1.0.0 are read unchanged.
- Clear pairing also forgets AirPlay controllers, and that table replaces its oldest entry instead of refusing every phone after eight.
- Flashing instructions lead with the full image for a first install; app-only writes need this project's bootloader.
- Builds stop on ESP-IDF versions other than v5.5.3.
- Whole firmware built at -O2. Finished frames are packed for the display on the parsing core while the other core reconstructs the next one.
- Phone-call echo cancellation, ESP-SR (default) or SpeexDSP at build time.
- Battery level, low-battery icon over CarPlay and a warned automatic power-off.
- Tab5 Settings: microphone level, picture quality (applies from the next connection), picture sharpening, screen off when unplugged, right-hand drive.
- Quieter logs: no I2S error on audio start; a cold boot no longer reports `other_watchdog`.

## v1.0.0 — 2026-11-14

First versioned release of the wired CarPlay receiver for M5Stack Tab5 / ESP32-P4 v1.x.

- Video/touch, music/navigation audio mixing, microphone, persistent volume/brightness and battery charging.
- Waiting-screen Clear pairing with confirmation, disconnected-session checks and namespace-scoped erasure.
- Navigation stream ID recycling; USB channel error handling, TX/RX watchdogs, power-cycle recovery and task progress guard.
- SD runtime/crash logging and version identification.
- Upstream PCM starvation recovery, authenticated audio packet regression and required CI, adapted to dual audio lanes.
- Removed firmware demos, boot audio tests, maintenance reset mode and one-off source injection scripts; retained regression tests.
- Added isolated release builds, app/full BINs, configuration and SHA-256 manifests.

Known: switching to iPhone 17 Pro / iOS 26.5 may require restarting Tab5 after Clear pairing. Experimental authentication identity is not part of the public source; authenticated binaries remain local. No-auth builds cannot start CarPlay.

Details and validation boundaries: [v1.0.0 release notes](docs/releases/v1.0.0.md).
