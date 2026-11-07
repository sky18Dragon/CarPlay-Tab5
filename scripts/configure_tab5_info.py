#!/usr/bin/env python3
"""Normalize checked-in capability templates to implemented Tab5 features.
Run with --check in CI; no network or device access.
"""
import argparse
import plistlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'components/airplay_software_provider/assets'
FORMATS = 0x400 | 0x800 | 0x4000 | 0x8000  # 44.1/48 kHz, s16 mono/stereo

def normalize(d):
    d['name'] = 'Tab5 CarPlay'
    d['model'] = 'Tab5'
    d['manufacturer'] = 'CarPlay-Tab5 project'
    d['displays'] = [d['displays'][0]]
    d['hidDevices'][0]['name'] = 'Tab5 touchscreen'
    d['extendedFeatures'] = [x for x in d.get('extendedFeatures', []) if x != 'vocoderInfo']
    d['audioFormats'] = [dict(type=100, audioType=usage, audioOutputFormats=FORMATS)
                         for usage in ('default', 'media', 'alert')]
    d['audioLatencies'] = [dict(type=100, audioType=usage, inputLatencyMicros=0,
                               outputLatencyMicros=60000)
                          for usage in ('default', 'media', 'alert')]
    # No OEM-menu action exists in this application yet.
    for key in ('oemIcons', 'oemIconLabel'):
        d.pop(key, None)
    d['oemIconVisible'] = False
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
            assert len(path.read_bytes()) <= 4096
        else:
            path.write_bytes(plistlib.dumps(expected, fmt=plistlib.FMT_BINARY, sort_keys=False))
    print('Tab5 capability templates: single screen, supported PCM, no microphone/OEM menu')

if __name__ == '__main__':
    main()
