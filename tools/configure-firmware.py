#!/usr/bin/env python3
"""Select IPSW image names from its manifest, without guessing from filenames."""
import json
import pathlib
import plistlib
import sys
import zipfile

ipsw = pathlib.Path(sys.argv[1]).resolve()
base = pathlib.Path(__file__).resolve().parents[1] / 'vendor/hakios/sysimage'
with zipfile.ZipFile(ipsw) as archive:
    manifest = plistlib.loads(archive.read('BuildManifest.plist'))
    identities = [i for i in manifest['BuildIdentities']
                  if i['Info']['DeviceClass'] == 'd22ap']
    if not identities or manifest['ProductVersion'] != '16.7.16':
        raise SystemExit('Expected iPhone X iOS 16.7.16 firmware')
    images = identities[0]['Manifest']
    selected = {k: images[k]['Info']['Path'] for k in
                ('OS', 'Cryptex1,SystemOS', 'Cryptex1,AppOS', 'DeviceTree')}
    metadata = dict(path=str(ipsw), version=manifest['ProductVersion'],
                    build=manifest['ProductBuildVersion'], images=selected)
    for key, target in [('OS', 'rootfs.dmg'),
                        ('Cryptex1,SystemOS', 'cryptex-os.dmg'),
                        ('Cryptex1,AppOS', 'cryptex-app.dmg'),
                        ('DeviceTree', 'DeviceTree.d22ap.im4p')]:
        destination = base / target
        if destination.exists():
            if destination.stat().st_size != archive.getinfo(selected[key]).file_size:
                raise SystemExit(f'Existing input has wrong size: {destination}')
            continue
        print(f'Extracting {selected[key]} -> {destination}', flush=True)
        with archive.open(selected[key]) as source, destination.open('wb') as output:
            import shutil
            shutil.copyfileobj(source, output, 8 << 20)
    (base / 'firmware.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (base / 'cache-dir').write_text(manifest['ProductBuildVersion'] + '__iPhone10,3_6\n')
print(json.dumps(metadata, indent=2))
