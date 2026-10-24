from __future__ import annotations
import json, re, zipfile
from pathlib import Path
root=Path(__file__).resolve().parent
request=json.loads((root/'BUILD-REQUEST.json').read_text(encoding='utf-8-sig'))
expected=request['defaults']; assert expected=={'placement':'top-right','orientation':'north-up','mapSampleStep':2,'offset':0}
source=root/'source'; code=(source/'src/dllmain.cpp').read_text(encoding='utf-8-sig')
checks={
 'native placement enum default':r'g_minimapPlacement\s*\{\s*static_cast<int>\(MinimapPlacement::TopRight\)\s*\}',
 'native orientation enum default':r'g_minimapOrientation\s*\{\s*MinimapOrientation::NorthUp\s*\}',
 'configuration placement fallback':r'std::string configuredPosition\s*=\s*"top-right"\s*;',
}
for label,pattern in checks.items():
 assert re.search(pattern,code), f'missing {label}'
source_defaults=json.loads((source/'assets/mod.json').read_text(encoding='utf-8-sig'))['shroudtopia']['default']
assert source_defaults['position']==expected['placement'] and source_defaults['map_orientation']==expected['orientation']
with zipfile.ZipFile(root/'package/frozen-candidate.zip') as archive:
 packaged=json.loads(archive.read('mods/minimap_mod/mod.json'))['shroudtopia']['default']
assert packaged['position']==expected['placement'] and packaged['map_orientation']==expected['orientation']
assert source_defaults['map_sample_step']==expected['mapSampleStep'] and source_defaults['offset']==expected['offset']
assert packaged['map_sample_step']==expected['mapSampleStep'] and packaged['offset']==expected['offset']
print(json.dumps({'status':'PASS','scope':'frozen source and package settings contract; static, no game runtime','checks':list(checks),'sourceAndPackageDefaults':expected},separators=(',',':')))
