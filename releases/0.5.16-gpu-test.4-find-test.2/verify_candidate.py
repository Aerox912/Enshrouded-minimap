from __future__ import annotations
import hashlib, json, zipfile
from pathlib import Path
root=Path(__file__).resolve().parent
request=json.loads((root/'BUILD-REQUEST.json').read_text(encoding='utf-8-sig'))
manifest_path=root/'SOURCE-MANIFEST.json'; manifest_raw=manifest_path.read_bytes()
assert hashlib.sha256(manifest_raw).hexdigest()==request['sourceManifestSha256'], 'source manifest hash mismatch'
manifest=json.loads(manifest_raw.decode('utf-8-sig')); rows=manifest['files']; assert len(rows)==request['sourceFileCount']
accepted_manifest=(root/'ACCEPTED-SOURCE-MANIFEST.json').read_bytes(); assert hashlib.sha256(accepted_manifest).hexdigest()==request['acceptedSourceManifestSha256'], 'accepted source manifest hash mismatch'
source_patch=(root/'source.patch').read_bytes(); assert hashlib.sha256(source_patch).hexdigest()==request['sourcePatchSha256'], 'accepted source patch hash mismatch'
src=root/'source'; expected={r['path'] for r in rows}; actual={p.relative_to(src).as_posix() for p in src.rglob('*') if p.is_file()}
assert actual==expected, f"source inventory differs: missing={sorted(expected-actual)[:5]}, extra={sorted(actual-expected)[:5]}"
for row in rows:
 p=src.joinpath(*row['path'].split('/')); data=p.read_bytes(); assert len(data)==row['size'], f"size mismatch {row['path']}"; assert hashlib.sha256(data).hexdigest()==row['sha256'], f"hash mismatch {row['path']}"
archive=root/'package/frozen-candidate.zip'; archive_bytes=archive.read_bytes()
assert len(archive_bytes)==request['candidateArchiveBytes']; assert hashlib.sha256(archive_bytes).hexdigest()==request['candidateArchiveSha256']
with zipfile.ZipFile(archive) as z:
 assert len(z.infolist())==request['candidateArchiveMembers']; names=[i.filename for i in z.infolist()]; assert len(names)==len(set(names)); assert z.testzip() is None
 mod=json.loads(z.read('mods/minimap_mod/mod.json')); assert mod['version']==request['version']
 defaults=mod['shroudtopia']['default']; assert defaults['position']==request['defaults']['placement']; assert defaults['map_orientation']==request['defaults']['orientation']
 historical_dll=z.read('mods/minimap_mod/minimap_mod.dll')
 assert len(historical_dll)==request['historicalCandidateDllBytes']
 assert hashlib.sha256(historical_dll).hexdigest()==request['historicalCandidateDllSha256']
 assert historical_dll[:2]==b'MZ'; pe=int.from_bytes(historical_dll[0x3c:0x40],'little'); assert historical_dll[pe:pe+4]==b'PE\0\0'; assert int.from_bytes(historical_dll[pe+4:pe+6],'little')==0x8664
source_mod=json.loads((src/'assets/mod.json').read_text(encoding='utf-8-sig')); assert source_mod['version']==request['version']; source_defaults=source_mod['shroudtopia']['default']
assert source_defaults['position']==request['defaults']['placement']; assert source_defaults['map_orientation']==request['defaults']['orientation']
dllmain=(src/'src/dllmain.cpp').read_text(encoding='utf-8-sig')
assert 'MinimapPlacement::TopRight' in dllmain and 'MinimapOrientation::NorthUp' in dllmain
assert 'std::string configuredPosition = "top-right";' in dllmain
print(json.dumps({'status':'FROZEN_SOURCE_AND_HISTORICAL_INPUT_PASS','sourceFiles':len(rows),'sourceManifestSha256':hashlib.sha256(manifest_raw).hexdigest(),'historicalArchiveSha256':hashlib.sha256(archive_bytes).hexdigest(),'historicalArchiveMembers':len(names),'historicalDllSha256':request['historicalCandidateDllSha256'],'sourceDefaults':request['defaults'],'runtimeGameplayAccepted':False},separators=(',',':')))
