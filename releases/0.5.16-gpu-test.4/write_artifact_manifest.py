import hashlib, json, os, zipfile
from pathlib import Path
base=Path(__file__).resolve().parent
request=json.loads((base/'BUILD-REQUEST.json').read_text(encoding='utf-8-sig'))
dist=base/'dist'; dist.mkdir(exist_ok=True)
source=base/'package/frozen-candidate.zip'; new_dll=dist/'minimap_mod.dll'; target=dist/'Shroudweave-Minimap-0.5.16-gpu-test.4-private.zip'
receipt=json.loads((dist/'BUILD-RECEIPT.json').read_text(encoding='utf-8-sig'))
sha=lambda data: hashlib.sha256(data).hexdigest()
input_bytes=source.read_bytes()
assert len(input_bytes)==request['candidateArchiveBytes'] and sha(input_bytes)==request['candidateArchiveSha256']
dll_bytes=new_dll.read_bytes(); dll_sha=sha(dll_bytes)
assert len(dll_bytes)==receipt['build']['runtimeDllBytes'] and dll_sha==receipt['build']['runtimeDllSha256']
member='mods/minimap_mod/minimap_mod.dll'; unchanged=[]; replaced=0
with zipfile.ZipFile(source,'r') as original, zipfile.ZipFile(target,'w') as output:
    names=[entry.filename for entry in original.infolist()]
    assert len(names)==request['candidateArchiveMembers'] and len(names)==len(set(names))
    assert original.testzip() is None
    for entry in original.infolist():
        data=original.read(entry.filename)
        if entry.filename==member:
            data=dll_bytes; replaced+=1
        else:
            unchanged.append({'path':entry.filename,'bytes':len(data),'sha256':sha(data)})
        output.writestr(entry,data,compress_type=entry.compress_type)
assert replaced==1
asset_manifest_bytes=(json.dumps(unchanged,indent=2)+'\n').encode('utf-8')
(dist/'HISTORICAL-ASSET-MEMBER-MANIFEST.json').write_bytes(asset_manifest_bytes)
with zipfile.ZipFile(target,'r') as result:
    assert result.testzip() is None
    assert result.namelist()==names
    assert sha(result.read(member))==dll_sha
    for row in unchanged:
        data=result.read(row['path'])
        assert len(data)==row['bytes'] and sha(data)==row['sha256'], f"asset member changed: {row['path']}"
    mod=json.loads(result.read('mods/minimap_mod/mod.json'))
    assert mod['version']==request['version']
    defaults=mod['shroudtopia']['default']
    assert defaults['position']==request['defaults']['placement'] and defaults['map_orientation']==request['defaults']['orientation']
manifest={
 'schemaVersion':2,'status':'CI_ARTIFACT_NEW_MINIMAP_RUNTIME_CANDIDATE','candidate':request['candidate'],'version':request['version'],
 'managerCandidate':request['managerCandidate'],'channel':request['channel'],'repository':'Aerox912/Enshrouded-minimap',
 'sourceCommit':os.environ['GITHUB_SHA'],'workflowRunId':int(os.environ['GITHUB_RUN_ID']),'workflowRunAttempt':int(os.environ['GITHUB_RUN_ATTEMPT']),
 'mainPipeline':os.environ.get('GITHUB_EVENT_NAME')=='push' and os.environ.get('GITHUB_REF')=='refs/heads/main',
 'sourceManifestSha256':request['sourceManifestSha256'],'sourceFileCount':request['sourceFileCount'],
 'historicalInputArchiveSha256':request['candidateArchiveSha256'],'historicalInputDllSha256':request['historicalCandidateDllSha256'],
 'newRuntimeDllSha256':dll_sha,'newRuntimeDllBytes':len(dll_bytes),'runtimeSha256':dll_sha,'runtimeBytes':len(dll_bytes),'packageZipSha256':sha(target.read_bytes()),'packageZipBytes':target.stat().st_size,
 'packageMemberCount':len(names),'replacedMember':member,'unchangedAssetMemberCount':len(unchanged),'unchangedAssetMemberManifestSha256':sha(asset_manifest_bytes),
 'focusedDefaultsHarnessStatus':receipt['focusedDefaultsHarness']['status'],'focusedDefaultsHarnessManifestSha256':sha((dist/'receipts/focused-default-harness-input.json').read_bytes()),
 'runtimeGameplayAccepted':False,'releasePublished':False
}
(dist/'ARTIFACT-MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps(manifest,sort_keys=True))
