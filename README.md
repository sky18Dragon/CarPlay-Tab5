# CarPlay-Tab5

English | [简体中文](README.zh-CN.md)

A wired CarPlay receiver for M5Stack Tab5 (ESP32-P4). Connect an unlocked iPhone to USB-A. The P4 handles pairing, video, audio, touch and local settings, with OpenH264 software video decoding.

This is an independent experimental project, not Apple-certified. **The current repository includes a shared experimental identity. Its private key is public and extractable from authenticated firmware; it is not a confidential identity. A no-auth build cannot enter CarPlay.** See [authentication provenance and setup](docs/软件认证接入.md).

## Features

- USBMUX / Lockdown pairing, TLS, iAP2 and wired CarPlay sessions.
- 890×240 video targeting 16 fps, scaled by PPA to 1280×720; touch input.
- Music and navigation prompt mixing/ducking, microphone uplink and device-tested Siri.
- Persistent volume and brightness through the CarPlay “Tab5 设置” settings icon.
- **Clear pairing** on the waiting screen, with confirmation and disconnected-session checks. Clears only phone trust records.
- USB channel error handling, TX/RX stall detection, port power-cycle recovery and an independent 15-second progress guard.
- SD runtime/crash logging, charging control and battery operation.

See [v1.0.0 release notes](docs/releases/v1.0.0.md) for validation and limitations.

## Hardware and switching phones

This project was developed and tested on the **M5Stack Tab5 with the ST7121 display driver IC**. The target configuration is **ESP32-P4 v1.x at 360 MHz / 16 MiB flash / 32 MiB PSRAM**. P4 v3 and other boards are unverified. USB-C is for power and the console. USB-A is for the phone.

To switch phones: **unplug → Clear pairing → confirm → restart Tab5 → connect the new unlocked iPhone and confirm Trust**. An iPhone 17 Pro on iOS 26.5 needed a Tab5 restart after clearing; its failure cause remains unconfirmed. Pairing storage is limited, not an unlimited multi-phone database.

## Build

Use **ESP-IDF v5.5.3**, with other components pinned by `dependencies.lock`:

```sh
source <esp-idf-v5.5.3>/export.sh
python3 scripts/fetch_openh264.py
./scripts/build.sh                      # explicitly disables software authentication
./scripts/build.sh --software-auth      # bundled experimental identity from secrets/mfi
```

The current main branch includes `identity.pk8`, `certificate.p7b` and `certificate.der` in [secrets/mfi/](secrets/mfi/README.md); see the [acquisition and checksum guide](docs/软件认证接入.md). The original v1.0.0 tag and release attachments retain their original snapshot without these files. Build scripts explicitly set authentication ON/OFF instead of inheriting an old CMake choice.

Release builds use separate directories and regenerate configuration from tracked defaults:

```sh
python scripts/release.py --auth local  # functional local experimental identity
python scripts/release.py --auth none   # no identity; cannot enter CarPlay
```

Output: `artifacts/releases/v1.0.0/{local-auth,no-auth}/`, containing app/full BINs, image information, configuration, licenses, manifest and SHA256SUMS. `version.txt` supplies the firmware version. Existing performance/compiler settings are preserved.

## Flash

**First install on a Tab5** (factory firmware, M5Stack launcher, or any other project): write the full image at **0x0**. It installs this project's bootloader and partition table with the app, and erases saved pairing and settings in its range.

```sh
python -m esptool --chip esp32p4 -p <port> write_flash 0x0 <release-full.bin>
```

**Upgrade a Tab5 that already runs CarPlay-Tab5** by writing **only the app**, preserving pairing and settings:

```sh
python -m esptool --chip esp32p4 -p <port> write_flash 0x10000 <release-app.bin>
```

The app-only write is safe only on top of this project's bootloader. Another bootloader may expect a different layout, and the Tab5 then resets in a loop right after `entry 0x4ff29ed0` without showing anything. Recover by writing the full image at 0x0. If unsure, back up first: `python -m esptool --chip esp32p4 -p <port> read_flash 0 0x1000000 tab5-backup.bin`.

Never interchange the app/full offsets. The release partition table has 24 KiB NVS and a 4 MiB factory app; existing CarPlay-Tab5 devices do not need their partition table changed for an app upgrade.

## Tests and structure

```sh
./scripts/test_host.sh
./scripts/test_ui.sh
./scripts/test_crypto.sh
./scripts/test_decoder.sh build-release-local-auth
```

- `main/`: production application, transports, media, UI, power and logging.
- `main/core/`: host-testable session, buffers, pixels, touch and protocol logic.
- `components/`: AirPlay, authentication ports/providers and OpenH264 integration.
- `tests/`: regression tests, stubs and synthetic fixtures; not linked into firmware.
- `scripts/`: dependency fetch, build, release and test entry points.
- `tools/perf/`: offline analysis, stream replay and CRC tools; not linked into firmware.
- `docs/`: design, release notes and [execution history](docs/执行记录.md).

v1.0.0 removes the offline demo, boot audio test, flash-based pairing-reset maintenance mode, simulated UI branches and one-off `tools/perf/add_*.py` source-patching scripts. Decoder fixtures remain for host regression only. Historical commands in the execution log do not describe the current release procedure.

## Known limitations

- The firmware connects one iPhone at a time and its pairing storage holds about two phones. Pairing a further phone removes the stored records of the others, which then confirm Trust again. **Clear pairing** on the waiting screen still removes everything by hand.
- iPhone 12 Pro Max / iOS 16.1.1 has passed clear/re-pair/video/music testing. iPhone 17 Pro / iOS 26.5 has user-confirmed success after restarting Tab5; failure logs are unavailable.
- A previous hour-long music/navigation run had one unrequested disconnect, followed by recovery. Controlled USB recovery tests passed; this is not a zero-disconnect guarantee.
- Low source resolution and software decoding limit sharpness/frame rate. Music can lower video frame rate; occasional memory-related video rejection has been observed.
- I2S disable messages may appear on audio start. This board may report `other_watchdog` on cold boot; that alone does not prove a runtime watchdog fault.
- Wired PCM only. Complete phone-call behavior, echo cancellation, battery endurance and low-battery shutdown are unverified or unfinished. No wireless CarPlay.
- Flash/NVS encryption is disabled. The bundled experimental identity is public; keep other private identities, pairing records and local device recordings/logs out of the repository. The default AirPlay Pair-Setup code is `3939`.

## License

Includes GPL-3.0-or-later code adapted from [xmz28/Carplay-S3](https://github.com/xmz28/Carplay-S3). Other components retain their licenses; see [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`. Authentication material is not relicensed by this source license. Apple, CarPlay, iPhone and AirPlay are trademarks of Apple Inc.
