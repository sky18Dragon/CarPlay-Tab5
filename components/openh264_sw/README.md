# Tab5 portable OpenH264 2.6.0

Source is pinned to Cisco commit `652bdb7719f30b52b08e506645a7322ff1b2cc6f`.
Run `python3 scripts/fetch_openh264.py` before configuring ESP-IDF. The downloader
verifies each of 112 decoder files against the pinned Git tree's blob hashes.
Source lives in ignored `external/openh264`, never downloaded at firmware runtime.

The CMake adaptation is from Carplay-S3 commit
`79a9f169f275b283f2909c5a326ed03c716c0d02`. It preserves Cisco's BSD-2-Clause
license (LICENSE), generates PSRAM/thread wrappers, fixes nonzero LTR picture
indices and single-thread flush reference release. It compiles portable C++
without assembly. Vendor format warnings remain visible but are not fatal:
P4 newlib uses 32-bit long for int32_t, while upstream diagnostic strings use
`%d`. This is confined to the vendor target; project warnings remain errors.

`main/core/video_decoder.cpp` implements single-worker ownership and padded
Annex-B input. Views into decoded planes expire on the next decoder call.
The accepted output limit is 1280x720; this output check is not an SPS allocation
limit. Hardware performance and worst-case decoder allocation remain board-test
items. The queue is limited to 512 KiB and three complete access units.

After the ESP-IDF configuration generates the patched source, run
`./scripts/test_decoder.sh`. It uses the same reference-picture patches on the
host and checks synthetic High Profile and 32 LTR frames against recorded pixel
CRCs. This is actual host decoding, not an ESP32-P4 benchmark.
