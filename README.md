# CarPlay-Tab5

English | [简体中文](README.zh-CN.md)

A wired Apple CarPlay receiver that runs on the M5Stack Tab5 (ESP32-P4). The iPhone plugs into the Tab5's USB-A port. Video, touch and the CarPlay session are all handled on the ESP32-P4. There is no hardware H.264 decoder, so video is decoded in software with OpenH264.

> **Experimental.** This is a hobby and research project. It is not affiliated with or approved by Apple. Getting through iAP2 authentication needs identity material that is **not** in this repository (see [Authentication](#authentication)).

## Status

Current firmware (2026-11-08): 890×240 at 16 fps, with battery charging enabled.

| Item | State |
|---|---|
| USB enumeration, USBMUX, Lockdown pairing and TLS | Works on the test iPhone |
| iAP2 authentication and identification, CarPlay session start | Works with local experimental identity |
| H.264 video (High profile, CABAC) | About 15 fps at 890×240, 0 rejects over a 10-minute drag soak |
| Touch (HID digitizer) | Works; the user confirmed tap positions on the device |
| Physical replug | Recovers in about 6 s, no heap leak |
| Battery charging and running on battery | Charges at about 950 mA; keeps running after USB-C is unplugged |
| Audio (media, Siri, calls, microphone) | **Not verified on the device** |

Measured over a 10-minute simulated drag at 890×240@16:

| Display interval P50 | P90 | P95 | P99 | ≥ 200 ms |
|---|---|---|---|---|
| 66–67 ms | 81–84 ms | 91–99 ms | 119–137 ms | 0 |

## Hardware

- M5Stack Tab5 with ESP32-P4 **revision v1.x**. The build targets revisions below v3 (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`) and runs at 360 MHz. It is not overclocked.
- An iPhone with a cable to the Tab5 **USB-A** (host) port.
- USB-C for power and the serial console. An NP-F550 battery is optional.

## How it works

```text
iPhone ──USB──> NCM network ──TCP──> RTSP / screen stream
                                        │
                                        ▼
                video queue (6 units) → OpenH264
   core 1: CABAC parse ── frame pipeline ── core 0: reconstruct, deblock,
                                             border expand, PIE pack + sharpen
                                        │
                                        ▼
           PPA YUV→RGB, scale 23/16 × 3, rotate → MIPI-DSI 1280×720
```

- **Anamorphic layout.** The phone is asked for 890×240 pixels but told the display is 16:9. iOS then lays out an 890×500 screen and sends squeezed pixels. The PPA stretches them back to full screen. Fewer pixels give a higher frame rate, and the icon proportions stay normal.
- **Frame-level pipeline.** Core 1 parses the next frame while core 0 reconstructs the previous one. The pipeline uses two macroblock array sets and two stage contexts. Anything unusual, such as multiple slices or errors, drains the pipeline and falls back to the upstream path.
- **PIE SIMD.** Packing into the PPA YUV420 layout and the vertical luma sharpening use ESP32-P4 PIE instructions in `main/core/pixel_pie.S`. The output is byte-identical to the C version, and sharpening costs about 0.6 ms per frame.
- **L2 cache autoload** over PSRAM makes each frame about 7% faster.
- **Charging.** `main/power_status.c` turns the IP2326 charger on through the second IO expander. The BSP does not do this. It also logs battery voltage and current from the INA226 once a minute.

## Repository layout

| Path | Contents |
|---|---|
| `main/` | Application: USB host and NCM, USBMUX and Lockdown, iAP2, video output, touch/HID, audio, UI, power |
| `main/core/` | Hardware-independent code with host tests: session, video queue and decoder wrapper, pixel conversion, touch mapping, RTSP stream |
| `components/` | AirPlay control, RTSP, pairing, screen stream, timing, software identity, auth port, Ed25519, OpenH264 port (`openh264_sw`) |
| `components/airplay_software_provider/assets/` | `/info` plists, one per video profile. Each includes the HID touch descriptor |
| `tests/` | Host tests and fixtures |
| `scripts/` | Build, OpenH264 fetch, host, decoder and crypto test scripts |
| `tools/perf/` | Measurement helpers: log statistics, frame extraction, temporary source hooks, replay and CRC harness |
| `docs/` | Design notes and the full development log, in Chinese |

Documents (Chinese):
- [Development log](docs/执行记录.md): every change, measurement and reverted experiment.
- [Technical design](docs/技术实现方案.md)
- [On-device acceptance steps](docs/实机验收.md)
- [Software authentication](docs/软件认证接入.md)

## Build

Requirements: ESP-IDF **v5.5.3**, with components pinned by `dependencies.lock` (Tab5 BSP 1.3.2, LVGL 9.3.0).

```sh
source <esp-idf-v5.5.3>/export.sh
python3 scripts/fetch_openh264.py      # downloads the pinned OpenH264 into external/
./scripts/build.sh                     # or: idf.py build
```

The video height comes from `CONFIG_TAB5_VIDEO_PROFILE_HEIGHT` (default 240, which is the 890×240 profile). The other options are in `main/Kconfig.projbuild`.

### Authentication

The phone only starts CarPlay after iAP2 authentication succeeds. This project can embed an experimental software identity. To build with it:

1. Put `identity.pk8`, `certificate.p7b` and `certificate.der` in `secrets/mfi/`. This folder is git-ignored and nothing in it is published.
2. Configure with `-DTAB5_SOFTWARE_AUTH=ON`.

Without the identity, the firmware still builds and runs, but the phone stops with `authentication_identity_missing`. The option is cached in `build/CMakeCache.txt`, so check it after you reconfigure. A firmware image built with the identity contains extractable key material, so do not distribute it. See `docs/软件认证接入.md` for provenance.

### Flash

Write only the app partition. This keeps NVS, which holds the phone pairing record and the AirPlay identity:

```sh
python -m esptool --chip esp32p4 -p <port> write_flash 0x10000 build/carplay_tab5.bin
```

## Tests

```sh
./scripts/test_host.sh       # host unit tests (session, USB, iAP2, RTSP, pixel conversion, touch, ...)
./scripts/test_decoder.sh    # decoder regression on synthetic High-profile fixtures (build the firmware first)
./scripts/test_crypto.sh     # crypto tests against the ESP-IDF Mbed TLS sources
```

The per-frame CRC replay in `tools/perf/replay/` was checked against recorded iPhone streams. Those recordings show phone screen content, so they are not published.

## Known issues

- **Frame rate is limited by the phone.** About 1 frame in 20 arrives late or is skipped by the iPhone. Old and new decoders both measure 15.1–15.4 fps under the same conditions. The 15.8 fps target was not reached at 16 fps. 240@17 reached 15.7 fps, but its queue overflowed about once per 10 minutes and latency rose.
- **Image is still soft.** The phone sends 240 lines and they are scaled 3× vertically with bilinear PPA. Sharpening helps, but small text is less crisp than on a native-resolution display.
- **Extra reject or IDR right after connecting (240 profiles).** Earlier runs sometimes showed one extra rejected frame and IDR in the first 1–2 s, seen as one stutter when video starts. It did not appear in the latest replug test. Not investigated.
- **iPhone does not re-enumerate after a long USB-A power-off.** After the 5 V port is off for a long time, the iPhone needs a physical replug. Short power-offs and normal replugs recover in about 6 s.
- **USB host assertion.** An assertion in `usb_dwc_hal_chan_decode_intr` occurred once under decoder overload. The cause is unknown, and it has not reappeared.
- **Audio is unverified.** Media playback, Siri, phone calls and microphone uplink have not been checked on the device.
- **Battery.** Runtime on a full charge has not been measured. The charge current is fixed at the 1000 mA step. There is no battery indicator or low-battery shutdown. The IP2326 status pin (P6) does not reliably show charging.
- **Security.** NVS and flash encryption are off. The default AirPlay Pair-Setup code is `3939`. The experimental iAP2 identity can be extracted from the firmware.
- **Compatibility.** Tested with one iPhone, iOS 16, on one Tab5 (P4 v1.3). It is wired only; wireless CarPlay is not implemented.
- **Real touch-to-display latency** has not been measured with a camera. The latency figures above are from firmware logs.
- **Measurement scripts are development aids.** `tools/perf/add_*.py` patch source files temporarily and must be reverted after use. The runner scripts that flash and capture logs are machine-specific and are not included.

## Licence and credits

This project includes code adapted from [xmz28/Carplay-S3](https://github.com/xmz28/Carplay-S3) under **GPL-3.0-or-later**, so firmware built from this repository is GPL-covered. Other parts carry their own licences: OpenH264 (BSD-2-Clause), orlp/ed25519 (zlib), and the ESP-IDF, BSP and LVGL packages. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.

Apple, CarPlay, iPhone and AirPlay are trademarks of Apple Inc.
