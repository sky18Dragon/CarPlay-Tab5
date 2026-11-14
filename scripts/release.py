#!/usr/bin/env python3
"""Build explicit v1 release variants without reusing developer sdkconfig/auth flags."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def run(*args, capture=False):
    return subprocess.run(args, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None).stdout

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--auth', choices=('local', 'none'), required=True,
                        help='local embeds private identity; none cannot start CarPlay')
    args = parser.parse_args()
    if not os.environ.get('IDF_PATH') or run('idf.py', '--version', capture=True).strip() != 'ESP-IDF v5.5.3':
        parser.error('Source ESP-IDF v5.5.3/export.sh first')
    version = (ROOT / 'version.txt').read_text().strip()
    if version != '1.0.0':
        parser.error('Review the release procedure before changing the release version')
    variant = 'local-auth' if args.auth == 'local' else 'no-auth'
    build = ROOT / ('build-release-' + variant)
    output = ROOT / 'artifacts/releases' / ('v' + version) / variant
    build.mkdir(exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    # This config belongs to this script; regenerate it from tracked defaults.
    config = build / 'sdkconfig'
    config.write_text('')
    run('idf.py', '-B', str(build), '-DIDF_TARGET=esp32p4',
        '-DSDKCONFIG=' + str(config), '-DSDKCONFIG_DEFAULTS=' + str(ROOT / 'sdkconfig.defaults'),
        '-DTAB5_SOFTWARE_AUTH=' + ('ON' if args.auth == 'local' else 'OFF'), 'build')
    image = build / 'carplay_tab5.bin'
    # ESP-IDF app descriptor is at the beginning of the first image segment.
    data = image.read_bytes()
    if data[32:36] != bytes.fromhex('3254cdab') or data[48:80].split(b'\0')[0].decode() != version:
        raise RuntimeError('Image version does not match version.txt')
    for marker in (b'offline development baseline', b'Offline synthetic media', b'TEST_ONLY', b'USB_TEST'):
        if marker in data:
            raise RuntimeError('Test marker remains in release: ' + marker.decode())
    stem = 'carplay-tab5-v' + version + '-' + variant
    app = output / (stem + '-app.bin')
    full = output / (stem + '-full.bin')
    shutil.copyfile(image, app)
    run(sys.executable, '-m', 'esptool', '--chip', 'esp32p4', 'merge_bin',
        '-o', str(full), '--flash_mode', 'dio', '--flash_size', '16MB', '--flash_freq', '80m',
        '0x2000', str(build / 'bootloader/bootloader.bin'),
        '0x8000', str(build / 'partition_table/partition-table.bin'), '0x10000', str(image))
    info = run(sys.executable, '-m', 'esptool', '--chip', 'esp32p4', 'image_info', str(app), capture=True)
    (output / 'image-info.txt').write_text(info)
    shutil.copyfile(config, output / 'sdkconfig')
    shutil.copyfile(ROOT / 'partitions.csv', output / 'partitions.csv')
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copyfile(ROOT / name, output / name)
    shutil.copytree(ROOT / 'licenses', output / 'licenses', dirs_exist_ok=True)
    note = ('LOCAL USE ONLY: contains extractable experimental authentication private key. Do not upload.\n'
            if args.auth == 'local' else
            'NO AUTHENTICATION IDENTITY: this build cannot enter CarPlay. Build with your own identity for CarPlay.\n')
    (output / 'FLASH-INSTRUCTIONS.txt').write_text(note + '\n'
        'Hardware: M5Stack Tab5, ESP32-P4 v1.x, 16 MiB flash. ESP-IDF v5.5.3.\n'
        'Upgrade existing Tab5 (keeps pairing/settings):\n'
        f'python -m esptool --chip esp32p4 -p PORT write_flash 0x10000 {app.name}\n\n'
        'Fresh installation ONLY (replaces partition table and ERASES pairing/settings in its address range):\n'
        f'python -m esptool --chip esp32p4 -p PORT write_flash 0x0 {full.name}\n\n'
        'Do not flash the app BIN at 0x0 or the full BIN at 0x10000.\n'
        'For switching phones: unplug, Clear pairing, restart Tab5, then connect the new unlocked phone.\n')
    manifest = {'version': version, 'variant': variant, 'software_auth': args.auth == 'local',
                'public_upload_allowed': args.auth == 'none', 'idf': 'v5.5.3',
                'source_commit_at_build': run('git', 'rev-parse', 'HEAD', capture=True).strip(),
                'working_tree_dirty_at_build': bool(run('git', 'status', '--porcelain', capture=True).strip()),
                'hardware': 'Tab5 / ESP32-P4 v1.x / 16 MiB flash',
                'images': [{'file': p.name, 'bytes': p.stat().st_size, 'sha256': sha(p), 'flash_offset': offset}
                           for p, offset in ((app, '0x10000'), (full, '0x0'))],
                'sdkconfig_sha256': sha(config), 'dependencies_sha256': sha(ROOT / 'dependencies.lock')}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    files = sorted(p for p in output.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
    (output / 'SHA256SUMS').write_text(''.join(sha(p) + '  ' + p.relative_to(output).as_posix() + '\n' for p in files))
    print('Release prepared: ' + str(output))

if __name__ == '__main__':
    main()
