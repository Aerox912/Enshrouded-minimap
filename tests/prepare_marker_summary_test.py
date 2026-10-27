import pathlib,re,hashlib,json
root=pathlib.Path(__file__).resolve().parents[1]
s=(root/'src/dllmain.cpp').read_text(encoding='utf-8')
def block(prefix,semicolon=False):
 start=s.index(prefix);begin=s.index('{',start);depth=1;i=begin+1
 while depth:
  if s[i]=='{':depth+=1
  if s[i]=='}':depth-=1
  i+=1
 return s[start:i+(1 if semicolon else 0)]
constants=[]
for name in ['FIXED_32_32_TO_WORLD','VISIBLE_MAP_MARKER_MAX_ENTRIES','VISIBLE_MAP_MARKER_STALE_MS','VISIBLE_MAP_MARKER_PRUNE_MS']:
 constants.append(next(line.strip() for line in s.splitlines() if 'constexpr ' in line and name+' =' in line))
parts=constants+[block('    struct CapturedMapMarkerVisibility',True),block('    float FixedToWorld('),block('    bool SameVisibleMapMarkerIdentity(')]
parts.append('std::mutex g_visibleMapMarkerMutex; std::vector<CapturedMapMarkerVisibility> g_visibleMapMarkers; DWORD g_lastVisibleMapMarkerPruneTick=0,g_lastVisibleMapMarkerSummaryTick=0; std::atomic<bool> g_debugLoggingEnabled{false}; std::uint64_t g_markerPositionRetryEpoch=0;')
parts.append(block('    void PruneVisibleMapMarkersLocked('))
parts.append(block('    bool SameMarkerResolverSnapshot('))
f=block('    void PublishVisibleMapMarker(')
needle='for (const CapturedMapMarkerVisibility& current : g_visibleMapMarkers)\n            {'
assert f.count(needle)==1
# Observe work count only; all production data and branch decisions remain intact.
f=f.replace(needle,needle+'\n                ++summaryVisits;')
parts.append(f)
(root/'tests/marker_summary_extracted.inc').write_text('\n'.join(parts),encoding='utf-8')
print(json.dumps({'productionSha256':hashlib.sha256((root/'src/dllmain.cpp').read_bytes()).hexdigest(),'instrumentation':'one summary-loop visit counter in extracted function, no production edit'}))
