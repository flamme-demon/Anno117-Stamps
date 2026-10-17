"""Assemble the player ZIP from the compiled DLL and the complete mod."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import struct
import tempfile
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / 'mods/anno117-stamps-native-ui'
version = json.loads((MOD / 'modinfo.json').read_text(encoding='utf-8'))['Version']
if not re.fullmatch(r'\d+\.\d+\.\d+', version):
    raise ValueError('modinfo.json must contain a three-part version.')
tag = os.environ.get('RELEASE_TAG', '') or 'v' + version
if not re.fullmatch(r'v?' + re.escape(version) + r'(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?', tag):
    raise ValueError(f'Release tag {tag!r} does not match mod version {version}.')
dll = ROOT / 'build/native-stamps-ui/dbghelp.dll'
data = dll.read_bytes()
if len(data) < 0x40 or data[:2] != b'MZ':
    raise ValueError('Missing Windows DLL. Compile it before packaging.')
pe = struct.unpack_from('<I', data, 0x3c)[0]
if (pe + 24 > len(data) or data[pe:pe+4] != b'PE\0\0'
        or struct.unpack_from('<H', data, pe+4)[0] != 0x8664
        or not struct.unpack_from('<H', data, pe+22)[0] & 0x2000
        or b'dbghelp_orig' not in data):
    raise ValueError('Expected the Windows x64 proxy DLL built by this project.')
if (MOD / 'native-ui/catalogue.tsv').read_bytes():
    raise ValueError('The release must not contain a personal stamp catalogue.')
for xml in MOD.rglob('*.xml'):
    ET.parse(xml)

output = ROOT / 'build/release'
output.mkdir(parents=True, exist_ok=True)
name = 'Anno117-Stamps-' + tag
with tempfile.TemporaryDirectory(prefix='anno117-release-') as directory:
    package = Path(directory) / name
    package.mkdir()
    shutil.copytree(MOD, package / 'mods' / MOD.name)
    shutil.copy2(dll, package / 'dbghelp.dll')
    for filename in ('README.md', 'LICENSE', 'install-linux.sh', 'install-windows.ps1'):
        shutil.copy2(ROOT / filename, package / filename)
    (package / 'licenses').mkdir()
    shutil.copy2(ROOT / 'external/minhook/LICENSE.txt', package / 'licenses/MinHook.txt')
    shutil.copy2(ROOT / 'external/miniz/LICENSE', package / 'licenses/miniz.txt')
    archive = output / (name + '.zip')
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for path in sorted(package.rglob('*')):
            if path.is_symlink():
                raise ValueError(f'Symlinks are not allowed in the release: {path}')
            if path.is_file():
                info = zipfile.ZipInfo(path.relative_to(package.parent).as_posix())
                info.create_system = 3
                info.external_attr = (0o100755 if path.name == 'install-linux.sh' else 0o100644) << 16
                zipped.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip() is not None:
            raise ValueError('Invalid release ZIP.')
shutil.copy2(dll, output / 'dbghelp.dll')
files = (output / 'dbghelp.dll', archive)
(output / 'SHA256SUMS.txt').write_text(''.join(
    hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n' for path in files
), encoding='ascii')
print(f'Package: {archive}')
