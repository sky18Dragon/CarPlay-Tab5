#!/usr/bin/env python3
"""Normalize checked-in capability templates to implemented Tab5 features.
Run with --check in CI; no network or device access.
"""
import argparse
import plistlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'components/airplay_software_provider/assets'
# PCM s16 format bits (DiPlay AirPlayInfoPlist.kt, 48 kHz entertainment rate).
PCM_VOICE = 0x3fc                      # 8/16/24/32 kHz, mono and stereo
PCM = PCM_VOICE | 0xc000               # plus 48 kHz mono and stereo
PCM_MONO = 0x154 | 0x4000              # 8/16/24/32/48 kHz mono
PCM_MONO_IN = 0x54 | 0x4000            # microphone: 8/16/24/48 kHz mono (whole fractions of 48 kHz)
# Declaring a microphone input is what makes the iPhone route CarPlay audio to the
# receiver (DiPlay issue #288). No Opus or AAC: only PCM is implemented.
AUDIO = [  # (type, audioType, output formats, input formats)
    (100, 'compatibility', PCM, PCM_MONO_IN),
    (101, 'compatibility', PCM, None),
    (100, 'default', PCM, PCM_MONO_IN),
    (100, 'alert', PCM, None),
    (100, 'media', PCM, None),
    (100, 'telephony', PCM_MONO, PCM_MONO_IN),
    (100, 'speechRecognition', PCM_MONO, PCM_MONO_IN),
    (101, 'default', PCM, None),
]
LATENCIES = [(100, None), (100, 'default'), (100, 'media'), (100, 'telephony'),
             (100, 'speechRecognition'), (100, 'alert'), (101, None), (101, 'default')]

def normalize(d):
    d['name'] = 'Tab5 CarPlay'
    d['model'] = 'Tab5'
    d['manufacturer'] = 'CarPlay-Tab5 project'
    d['displays'] = [d['displays'][0]]
    d['hidDevices'][0]['name'] = 'Tab5 touchscreen'
    d['extendedFeatures'] = ['vocoderInfo', 'enhancedRequestCarUI']
    formats = []
    for kind, usage, output, inputs in AUDIO:
        entry = dict(type=kind, audioType=usage, audioOutputFormats=output)
        if inputs is not None:
            entry['audioInputFormats'] = inputs
        formats.append(entry)
    d['audioFormats'] = formats
    latencies = []
    for kind, usage in LATENCIES:
        entry = dict(type=kind, inputLatencyMicros=0, outputLatencyMicros=0)
        if usage is not None:
            entry['audioType'] = usage
        latencies.append(entry)
    d['audioLatencies'] = latencies
    # CarPlay home-screen icon; tapping it sends requestUI, which opens the
    # local settings page (volume, brightness).
    d['oemIconVisible'] = True
    d['oemIconLabel'] = 'Tab5 设置'
    d['oemIcons'] = [dict(imageData=(ASSETS / 'oem_settings.png').read_bytes(),
                          widthPixels=128, heightPixels=128, prerendered=True)]
    return d

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    check = parser.parse_args().check
    for path in sorted(ASSETS.glob('info*.plist')):
        original = plistlib.loads(path.read_bytes())
        expected = normalize(plistlib.loads(path.read_bytes()))
        if check:
            assert original == expected, f'{path}: capability mismatch'
            assert len(original['displays']) == 1
            assert len(path.read_bytes()) <= 3500  # AIRPLAY_RTSP_SESSION_INFO_MAX_BYTES
        else:
            path.write_bytes(plistlib.dumps(expected, fmt=plistlib.FMT_BINARY, sort_keys=False))
    print('Tab5 capability templates: single screen, PCM with microphone input, settings icon')

if __name__ == '__main__':
    main()
