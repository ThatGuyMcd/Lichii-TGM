#!/usr/bin/env python3
"""Verify public release identity, complete test receipts and checksum coverage."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET
import zipfile

def require(condition, message):
    if not condition:
        raise ValueError(message)

def appimage_offset(data):
    if data[:4] != b'\x7fELF' or data[8:11] != b'AI\x02':
        raise ValueError('Not a type2 AppImage')
    offset = 0
    while True:
        offset = data.find(b'hsqs', offset + 1)
        if offset < 0:
            raise ValueError('AppImage filesystem not found')
        if offset + 96 <= len(data):
            major, minor = struct.unpack_from('<HH', data, offset + 28)
            used = struct.unpack_from('<Q', data, offset + 40)[0]
            if (major, minor) == (4, 0) and 96 <= used <= len(data) - offset:
                return offset

def verify(folder, tag, source):
    if not re.fullmatch(r'Lichii-tgm-v\d+\.\d+\.\d+', tag):
        raise ValueError('Invalid fork patch tag')
    manifest = json.loads((folder / 'release-manifest.json').read_text())
    require(manifest['source_commit'] == source and manifest['release_tag'] == tag, 'Source/tag mismatch')
    sums = {}
    for line in (folder / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        name = name.lstrip('*')
        if Path(name).name != name or name in sums or not re.fullmatch('[0-9a-fA-F]{64}', digest):
            raise ValueError('Invalid checksum entry')
        sums[name] = digest.lower()
    required = [f'{tag}-windows-x86_64.zip', f'{tag}-linux-x86_64.AppImage',
                'release-manifest.json', 'windows-ctest.xml', 'linux-ctest.xml']
    for name in required:
        require(name in sums, 'Required asset is not checksummed: ' + name)
    for path in folder.iterdir():
        if path.is_file() and path.name != 'SHA256SUMS':
            require(path.name in sums, 'Unlisted release asset')
    for name, expected in sums.items():
        with (folder / name).open('rb') as stream:
            require(hashlib.file_digest(stream, 'sha256').hexdigest() == expected, 'Checksum mismatch: ' + name)
    for platform in ('windows', 'linux'):
        report = ET.parse(folder / (platform + '-ctest.xml')).getroot()
        cases = report.findall('.//testcase')
        require(len(cases) >= 16 and int(report.attrib['tests']) == len(cases), 'Incomplete CTest receipt')
        require(len({case.get('name') for case in cases}) == len(cases), 'Duplicate test groups')
        require(int(report.get('failures', 0)) == int(report.get('errors', 0)) == 0, 'CTest failures')
        require(not report.findall('.//failure') and not report.findall('.//error'), 'Failed CTest groups')
        require(not report.findall('.//skipped'), 'Skipped CTest groups')
        require(manifest['validation'][platform + '_ctest_groups'] == len(cases), 'CTest count mismatch')
    with zipfile.ZipFile(folder / required[0]) as package:
        require(package.testzip() is None, 'ZIP integrity failure')
        info = json.loads(package.read('build-info.json'))
        require(info['source_commit'] == source and info['release_tag'] == tag, 'Windows package identity mismatch')
        for program in ('lichii-tgm.exe', 'lichii-tgm-cli.exe', 'lichii-tgm-shader-prepare.exe'):
            require(source.encode() in package.read(program), 'Executable identity mismatch')
        require('LICENSE.txt' in package.namelist(), 'Package license missing')
    return appimage_offset((folder / required[1]).read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--source', required=True)
    args = parser.parse_args()
    print(verify(args.assets, args.tag, args.source))

if __name__ == '__main__':
    main()
