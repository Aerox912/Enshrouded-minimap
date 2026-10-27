#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include "../src/background_diagnostic.h"
DWORD fakeNow=200000;
DWORD TestTick(){return fakeNow;}
#define GetTickCount TestTick
volatile unsigned long long summaryVisits=0;
std::string lastLog;
void Log(const std::string& s){lastLog=s;}
// Generated below: exact production types, constants and function bodies.
#include "marker_summary_extracted.inc"
int failures=0, checks=0;
void Check(bool value,const char* name){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<name<<"\n";}
CapturedMapMarkerVisibility Marker(uintptr_t id,bool positioned=true){CapturedMapMarkerVisibility m{};m.stateAddress=id;m.kind=1;m.visibility=1;m.lastUpdateTick=fakeNow;m.hasWorldPosition=positioned;m.x=static_cast<std::int64_t>(id)<<36;m.z=84;return m;}
void Reset(bool debug=false){fakeNow=200000;g_visibleMapMarkers.clear();g_lastVisibleMapMarkerPruneTick=fakeNow;g_lastVisibleMapMarkerSummaryTick=0;g_debugLoggingEnabled=debug;summaryVisits=0;lastLog.clear();}
int main(){
 Reset();for(uintptr_t i=1;i<=2048;++i)g_visibleMapMarkers.push_back(Marker(i));auto m=Marker(2048);m.kind=2;PublishVisibleMapMarker(std::move(m));
 Check(summaryVisits==0 && lastLog.empty() && g_visibleMapMarkers.size()==2048 && g_visibleMapMarkers.back().kind==2,"debug-off skips summary traversal but updates full cache");
 std::cout<<"DEBUG_OFF_SUMMARY_VISITS="<<summaryVisits<<"\n";
 Reset(true);g_visibleMapMarkers.push_back(Marker(1));PublishVisibleMapMarker(Marker(1));
 Check(summaryVisits==0 && lastLog.empty(),"unchanged marker skips summary traversal");
 Reset(true);g_visibleMapMarkers.push_back(Marker(1));g_lastVisibleMapMarkerSummaryTick=fakeNow-4999;m=Marker(1);m.kind=2;PublishVisibleMapMarker(std::move(m));
 Check(summaryVisits==0 && lastLog.empty() && g_visibleMapMarkers[0].kind==2,"cooldown skips summary traversal but stores change");
 Reset(true);g_visibleMapMarkers.push_back(Marker(1));m=Marker(2);m.visibility=0;g_visibleMapMarkers.push_back(m);m=Marker(3);m.lastUpdateTick=fakeNow-VISIBLE_MAP_MARKER_STALE_MS-1;g_visibleMapMarkers.push_back(m);PublishVisibleMapMarker(Marker(4,false));
 Check(summaryVisits==4 && lastLog.find("visible=2 | positioned=1")!=std::string::npos && g_lastVisibleMapMarkerSummaryTick==fakeNow,"eligible summary preserves visible and positioned counts");
 Reset();g_visibleMapMarkers.push_back(Marker(1));m=Marker(1,false);m.x=m.z=0;PublishVisibleMapMarker(std::move(m));
 Check(g_visibleMapMarkers[0].hasWorldPosition && g_visibleMapMarkers[0].x==(std::int64_t{1}<<36) && g_visibleMapMarkers[0].z==84,"debug-off preserves previously known position");
 Reset();g_visibleMapMarkers.push_back(Marker(1));m=Marker(2);m.lastUpdateTick=0;g_visibleMapMarkers.push_back(m);g_lastVisibleMapMarkerPruneTick=0;PublishVisibleMapMarker(Marker(3));
 Check(g_visibleMapMarkers.size()==2 && g_visibleMapMarkers[0].stateAddress==1 && g_visibleMapMarkers[1].stateAddress==3,"debug-off retains stale marker pruning");
 Reset();for(uintptr_t i=1;i<=2048;++i)g_visibleMapMarkers.push_back(Marker(i));PublishVisibleMapMarker(Marker(4096));
 Check(g_visibleMapMarkers.size()==2048 && lastLog.empty(),"marker capacity remains bounded");
 Reset(true);fakeNow=4000;g_lastVisibleMapMarkerPruneTick=fakeNow;g_lastVisibleMapMarkerSummaryTick=0xffffffffu-1000;PublishVisibleMapMarker(Marker(1));
 Check(summaryVisits==1 && !lastLog.empty(),"summary cooldown handles tick wraparound");
 std::cout<<"CASES="<<checks<<" FAILURES="<<failures<<"\n";return failures?1:0;
}
