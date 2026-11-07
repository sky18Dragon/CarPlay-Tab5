"""Fetch and verify the portable OpenH264 decoder; portable curl downloader."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
REV = '652bdb7719f30b52b08e506645a7322ff1b2cc6f'  # Cisco v2.6.0

def download(url, path):
    subprocess.run(['curl', '-L', '--fail', '-H', 'Accept: application/vnd.github.raw+json', '--retry', '2', '--connect-timeout', '10',
                    '--max-time', '90', '--silent', '--show-error', url, '-o', str(path)], check=True)

def main():
    manifest = ROOT / 'artifacts/openh264-tree.json'
    manifest.parent.mkdir(parents=True, exist_ok=True)
    if not manifest.exists():
        download(f'https://api.github.com/repos/cisco/openh264/git/trees/{REV}?recursive=1', manifest)
    tree = json.loads(manifest.read_text())
    def wanted(x):
        p = x['path']
        return x['type'] == 'blob' and (p in ('LICENSE', 'README.md', 'RELEASES') or
            p.startswith(('codec/api/', 'codec/common/inc/', 'codec/common/src/',
                          'codec/decoder/core/inc/', 'codec/decoder/core/src/',
                          'codec/decoder/plus/inc/', 'codec/decoder/plus/src/')))
    items = [x for x in tree['tree'] if wanted(x)]
    def correct(x, data):
        return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() == x['sha']
    def cached(x):
        path=ROOT / 'external/openh264' / x['path']
        return path.is_file() and correct(x,path.read_bytes())
    missing=[x for x in items if not cached(x)]
    # One streamed archive avoids a separate API request for every source file.
    # Stop after portable decoder files; large upstream test media is not needed.
    if len(missing)>8:
        remaining={x['path']:x for x in missing}
        process=subprocess.Popen(['curl','-L','--fail','--connect-timeout','10',
            '--max-time','600','--silent','--show-error',
            f'https://codeload.github.com/cisco/openh264/tar.gz/{REV}'],stdout=subprocess.PIPE)
        try:
            with tarfile.open(fileobj=process.stdout,mode='r|gz') as archive:
                for member in archive:
                    path=member.name.split('/',1)[-1]
                    if member.isfile() and path in remaining:
                        data=archive.extractfile(member).read()
                        if not correct(remaining[path],data): raise RuntimeError('Git blob mismatch: '+path)
                        dest=ROOT / 'external/openh264' / path
                        dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(data)
                        del remaining[path]
                        if not remaining: break
            if remaining: raise RuntimeError(f'Archive missing {len(remaining)} files; rerun to resume')
        finally:
            if process.poll() is None: process.terminate()
            process.wait()
            process.stdout.close()
        missing=[]
    def get(x):
        dest = ROOT / 'external/openh264' / x['path']
        if cached(x): return
        dest.parent.mkdir(parents=True, exist_ok=True)
        download(f'https://api.github.com/repos/cisco/openh264/contents/{x["path"]}?ref={REV}', dest)
        if not correct(x,dest.read_bytes()): raise RuntimeError('Git blob mismatch: '+x['path'])
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(get, missing))
    print(f'Verified {len(items)} portable decoder files at {REV}')

if __name__ == '__main__': main()
