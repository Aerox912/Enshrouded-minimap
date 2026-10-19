// Test the production gates on a private PE mapping, without executing game code.
#include "../src/dllmain.cpp"
#include <iostream>
#include <future>
#include <thread>
void Check10(bool ok, const char* label) { if(!ok) { std::cerr<<"FAIL: "<<label<<'\n';std::exit(1); } std::cout<<"PASS: "<<label<<'\n'; }
template<class T> void Put10(void* p,size_t offset,T value) { std::memcpy(static_cast<unsigned char*>(p)+offset,&value,sizeof(value)); }
std::uint32_t local10[2]={0,2};
void __fastcall Init10(void*,void*,std::uint32_t size) { Check10(size==0x50,"camera record size is bounded"); }
bool __fastcall Next10(void* ctx,void* data,std::uint32_t size) {
    Check10(size==0x50,"camera advance record size");
    static_cast<std::uint64_t*>(ctx)[1]=1;static_cast<uintptr_t*>(data)[3]=reinterpret_cast<uintptr_t>(local10);return true;
}
int presentCalls10=0;
std::promise<void>* enteredPresent10=nullptr;
std::shared_future<void> releasePresent10;
std::int32_t __fastcall Present10(void*,const void*) {
    ++presentCalls10;
    if(enteredPresent10) { enteredPresent10->set_value();releasePresent10.wait(); }
    return 123;
}
void CheckInactive10()
{
    ModContext context{};context.Log=[](const char*){};
    MinimapMod mod;
    g_originalQueuePresent.store(reinterpret_cast<uintptr_t>(&Present10));
    g_runtimeCallbacksActive.store(true);
    mod.Deactivate(&context);
    g_lastConfigPollTick=0;
    Check10(HookQueuePresent(nullptr,nullptr)==123 && presentCalls10==1,"inactive present still forwards to Vulkan exactly once");
    Check10(g_lastConfigPollTick==0,"inactive present must not poll controls or recreate notices");
    g_runtimeCallbacksActive.store(true);
    Check10(HookQueuePresent(nullptr,nullptr)==123 && presentCalls10==2 && g_lastConfigPollTick!=0,"reenabled callback resumes controls and forwards once");
    g_runtimeCallbacksActive.store(false);
}
void CheckLifecycle10()
{
    std::string lastLog;
    ModContext context{};context.Log=[&](const char* message){lastLog=message;};
    MinimapMod failed;
    failed.loaded=true; // Shroudtopia overwrites this even when Load rejected the profile.
    g_lastConfigPollTick=123;
    failed.Activate(&context);
    Check10(!failed.active && !g_runtimeCallbacksActive.load() && lastLog.find("activation skipped")!=std::string::npos,"host loaded flag cannot activate a rejected profile");
    failed.active=true;g_lastConfigPollTick=123;failed.Update(&context);
    Check10(g_lastConfigPollTick==123,"host active flag cannot update a rejected profile");

    MinimapMod mod;
    std::promise<void> entered,release;
    enteredPresent10=&entered;releasePresent10=release.get_future().share();
    g_originalQueuePresent.store(reinterpret_cast<uintptr_t>(&Present10));
    g_runtimeCallbacksActive.store(false); // Even pass-through callbacks must drain before teardown.
    auto present=std::async(std::launch::async,[]{return HookQueuePresent(nullptr,nullptr);});
    entered.get_future().wait();
    auto teardown=std::async(std::launch::async,[&]{mod.Deactivate(&context);});
    const bool waited=teardown.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout;
    release.set_value();
    Check10(present.get()==123,"in-flight original present finishes once");teardown.get();
    enteredPresent10=nullptr;
    Check10(waited,"deactivation drains original present before renderer teardown");
}
void __fastcall Create10() {}
void __fastcall Images10() {}
void __fastcall Foreign10() {}
uintptr_t failRestoreSlot10=0;
bool FailPresentRestore10(uintptr_t slot,uintptr_t value) {return slot!=failRestoreSlot10 && WritePointer(slot,value);}
void* __fastcall Resolve10(void*,const char* name) {
    if(!std::strcmp(name,"vkCreateSwapchainKHR")) return reinterpret_cast<void*>(&Create10);
    if(!std::strcmp(name,"vkGetSwapchainImagesKHR")) return reinterpret_cast<void*>(&Images10);
    if(!std::strcmp(name,"vkQueuePresentKHR")) return reinterpret_cast<void*>(&Present10);
    return nullptr;
}
void CheckVulkanOwnership10()
{
    RestoreVulkanTableHooks();
    std::array<uintptr_t,0x600/8> a{},b{};
    auto prepare=[](auto& table,uintptr_t device){
        table[1]=device;table[0x78/8]=reinterpret_cast<uintptr_t>(&Resolve10);
        table[0x580/8]=reinterpret_cast<uintptr_t>(&Create10);
        table[0x590/8]=reinterpret_cast<uintptr_t>(&Images10);
        table[0x5A0/8]=reinterpret_cast<uintptr_t>(&Present10);
    };
    const auto ta=reinterpret_cast<uintptr_t>(a.data()),tb=reinterpret_cast<uintptr_t>(b.data());
    CaptureVulkanDeviceTableInitHook(a.data(),nullptr,reinterpret_cast<void*>(11),nullptr);
    Check10(!TryInstallVulkanTableHooks(ta,11) && g_patchedVulkanDeviceTable.load()==0,"loader entry never patches an unfinished table");
    prepare(a,11);prepare(b,22);auto originalA=a,originalB=b;
    Check10(!TryInstallVulkanTableHooks(ta,22) && a==originalA,"present context device must equal table device");
    b[0x590/8]=reinterpret_cast<uintptr_t>(&Foreign10);
    Check10(!TryInstallVulkanTableHooks(tb,22) && b[0x580/8]==originalB[0x580/8],"foreign dispatch slot rejects whole table before writes");
    b=originalB;
    Check10(TryInstallVulkanTableHooks(tb,22) && a==originalA && g_patchedVulkanDeviceTable.load()==tb,"presenting table B owns originals even after loader entry A");
    Check10(!TryInstallVulkanTableHooks(ta,11) && a==originalA,"second table cannot inherit first table originals");
    b[1]=33;
    Check10(!TryInstallVulkanTableHooks(tb,33),"reused table address with another device is refused");
    b[1]=22;
    failRestoreSlot10=tb+0x5A0;
    Check10(!RestoreVulkanTableHooks(FailPresentRestore10) && b[0x5A0/8]==reinterpret_cast<uintptr_t>(&HookQueuePresent) && g_patchedVulkanDeviceTable.load()==tb && g_originalQueuePresent.load()==reinterpret_cast<uintptr_t>(&Present10),"failed present restoration retains tracked pass-through state");
    const int failedCalls=presentCalls10;
    Check10(HookQueuePresent(nullptr,nullptr)==123 && presentCalls10==failedCalls+1,"callback after failed restoration still forwards exactly once");
    b[0x590/8]=reinterpret_cast<uintptr_t>(&Foreign10);
    Check10(RestoreVulkanTableHooks(),"retry restores remaining owned slot");
    Check10(b[0x590/8]==reinterpret_cast<uintptr_t>(&Foreign10) && b[0x580/8]==originalB[0x580/8] && b[0x5A0/8]==originalB[0x5A0/8],"restoration preserves a later slot owner and restores only our slots");
    const int callsBefore=presentCalls10;
    Check10(HookQueuePresent(nullptr,nullptr)==123 && presentCalls10==callsBefore+1,"late callback selected before restoration still forwards once");
    Check10(g_callbackModulePinned && g_patchedVulkanDeviceTable.load()==tb && g_originalQueuePresent.load()==reinterpret_cast<uintptr_t>(&Present10),"callback code, original and ownership survive successful restoration until process exit");
}
void CheckReaders10()
{
    std::array<unsigned char,0x840> descriptor{};std::uint32_t entities[4]={2,0,0,0};
    Put10(descriptor.data(),0x828,reinterpret_cast<uintptr_t>(entities));
    std::uint64_t ctx[2]={reinterpret_cast<uintptr_t>(descriptor.data()),0};
    struct {std::uint64_t before=17;uintptr_t row[10]{};std::uint64_t after=19;} guarded;
    g_iterInit=Init10;g_iterNext=Next10;
    Check10(TryReadLocalCameraRecord(ctx,guarded.row),"current entity array selects the local player");
    Check10(ctx[1]==0 && guarded.before==17 && guarded.after==19,"camera preserves original cursor and buffer canaries");
    std::vector<unsigned char> entries(0xA0*2);
    for(int i=0;i<2;++i) {
        Put10(entries.data(),i*0xA0+0x10,WorldToFixed(1000.0f+i*100));
        Put10(entries.data(),i*0xA0+0x18,WorldToFixed(200));
        Put10(entries.data(),i*0xA0+0x20,WorldToFixed(1200));
        Put10(entries.data(),i*0xA0+0x30,std::uint32_t{WORLD_MAP_FLAME_ALTAR});
    }
    std::vector<unsigned char> state(0x3E6A90);
    Put10(state.data(),0x36EA00,reinterpret_cast<uintptr_t>(entries.data()));Put10(state.data(),0x36EA08,std::uint64_t{2});
    g_worldMapLayoutSupported=true;g_deathMarkerLayoutSupported=true;
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()),100);
    Check10(g_worldMapValid && g_worldMapMarkers.size()==2 && g_worldMapMarkers[1].x==WorldToFixed(1100),"current world array and A0 stride preserve both marker positions");
    std::array<unsigned char,0x48> list{};std::vector<unsigned char> arena(0x800*3);
    Put10(list.data(),0x18,std::uint32_t{52});Put10(list.data(),0x30,reinterpret_cast<uintptr_t>(arena.data()));Put10(list.data(),0x40,std::uint16_t{1});
    Put10(list.data(),0x38,std::uint64_t{3});
    Put10(arena.data(),2,std::uint16_t{3});
    for(int i=0;i<52;++i) {
        size_t start=(i<51 ? i*0x28 : 0x1000)+8;
        Put10(arena.data(),start+8,std::uint32_t(i+1));
        Put10(arena.data(),start+0x10,WorldToFixed(1000));Put10(arena.data(),start+0x18,WorldToFixed(200));Put10(arena.data(),start+0x20,WorldToFixed(1200));
    }
    auto savedArena=arena;auto savedList=list;std::vector<MinimapLive::Marker> pings;
    Check10(ReadPingEvents10(reinterpret_cast<uintptr_t>(list.data()),2,123,pings) && pings.size()==51 && pings.back().id==52,"segmented pings cross noncontiguous blocks and exclude local sender");
    Check10(arena==savedArena && list==savedList,"ping reader leaves the game arena unchanged");
    Put10(list.data(),0x38,UINT64_MAX);
    Check10(!ReadPingEvents10(reinterpret_cast<uintptr_t>(list.data()),0,123,pings) && pings.empty(),"overflowed native arena extent is rejected");
    Put10(list.data(),0x38,std::uint64_t{1});
    Check10(!ReadPingEvents10(reinterpret_cast<uintptr_t>(list.data()),0,123,pings) && pings.empty(),"readable next block outside native arena capacity is rejected");
    Put10(list.data(),0x40,std::uint16_t{3});
    Check10(!ReadPingEvents10(reinterpret_cast<uintptr_t>(list.data()),0,123,pings) && pings.empty(),"readable head block outside native arena capacity is rejected");
    Put10(list.data(),0x38,std::uint64_t{3});Put10(list.data(),0x40,std::uint16_t{1});
    Put10(arena.data(),2,std::uint16_t{0});
    Check10(!ReadPingEvents10(reinterpret_cast<uintptr_t>(list.data()),0,123,pings) && pings.empty(),"truncated ping chain publishes no partial result");
    Put10(arena.data(),2,std::uint16_t{1});
    Check10(!ReadPingEvents10(reinterpret_cast<uintptr_t>(list.data()),0,123,pings) && pings.empty(),"cyclic ping chain is rejected");
    Put10(list.data(),0x18,std::uint32_t{0});
    Check10(ReadPingEvents10(reinterpret_cast<uintptr_t>(list.data()),0,123,pings) && pings.empty(),"empty ping list does not dereference its arena");
    Check10(VulkanSlot(0x570)==0x580 && VulkanSlot(0x580)==0x590 && VulkanSlot(0x590)==0x5A0 && VulkanSlot(0x78)==0x78 && VulkanSlot(0xB8)==0xB8,"current Vulkan swapchain slots move without changing queue-submit or resolver slots");
    const std::uint8_t stolen[]={0x41,0xB8,0x50,0,0,0};auto thunk=BuildMapMarkerVisibilityLoopHookShellcode(stolen,sizeof(stolen));
    Check10(thunk[37]==0xE7 && std::equal(stolen,stolen+6,thunk.end()-11),"visibility thunk uses current record base and whole stolen instruction");
}
int main(int argc,char** argv)
{
    Check10(argc>=2,"client fixture supplied");
    Check10(MinimapClient10::MatchesFile(argv[1]),"exact current executable SHA256 is accepted");
    if(argc>2) Check10(!MinimapClient10::MatchesFile(argv[2]),"old executable cannot select current ABI");
    std::ifstream in(argv[1],std::ios::binary|std::ios::ate);
    Check10(in.good(),"read-only client fixture opens");
    std::vector<char> file(static_cast<size_t>(in.tellg()));in.seekg(0);in.read(file.data(),file.size());
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        const auto& s=sections[i];
        Check10(static_cast<size_t>(s.PointerToRawData)+s.SizeOfRawData<=file.size() && static_cast<size_t>(s.VirtualAddress)+s.SizeOfRawData<=image.size(),"bounded PE section");
        std::memcpy(image.data()+s.VirtualAddress,file.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    g_exeBase=reinterpret_cast<uintptr_t>(image.data());g_exeImageSize=image.size();
    for(auto rva:{0x1e02290,0x1e0c200,0x1e02460,0x1e06420,0x1e03360,0x1e08760,0x1e041d0,0x1e07210,0x1e0d770,0x1e11c00,0x1de8520})
        for(int word:{0,2}) { auto* p=reinterpret_cast<uintptr_t*>(image.data()+rva+word*8);*p=*p-nt->OptionalHeader.ImageBase+g_exeBase; }
    for(int i=0;i<4;++i) { auto* p=reinterpret_cast<uintptr_t*>(image.data()+0x1bbb4d0+i*0x28);*p=*p-nt->OptionalHeader.ImageBase+g_exeBase; }
    g_iterInit=reinterpret_cast<IterInitFn>(g_exeBase+0x9bab60);
    g_iterNext=reinterpret_cast<IterNextFn>(g_exeBase+0x9b5a50);
    g_client10=true;
    Check10(NamedClient10() && MatchCamera10() && MatchRenderer10(),"named camera and Vulkan contracts match current executable");
    Check10(HasVerifiedWorldMapLayout(),"current 1.0 world map contract is accepted");
    Check10(HasVerifiedDeathMarkerLayout() && HasVerifiedWorldClockLayout() && HasVerifiedWorldWeatherLayout() && HasVerifiedLiveFogLayout() && HasVerifiedLiveMarkerLayout(),"all optional current feature contracts match");
    Check10(IsNamedSystemAt(0x1de8520,"knowledge_query_mapmarker_visibility",0x414dc0) && MatchVisibility10(),"visibility is tied to the actual named system");
    for(auto rva:{0x2b29c0,0x9bab60,0x9b5a50,0x9b2e40,0x3b4e40}) {
        image[rva]^=1;Check10(!MatchCamera10(),"changed camera ABI is rejected");image[rva]^=1;
    }
    for(auto rva:{0xfd1a10,0xfd2663,0xfddfa0,0xfe0fb3,0xfe1d54}) {
        image[rva]^=1;Check10(!MatchRenderer10(),"changed renderer ABI is rejected");image[rva]^=1;
    }
    // Mutate the private PE mapping, then execute real gate/capture/copy behavior.
    for(const auto rva : {0x150ADE0, 0x310C6A, 0x310C67, 0xD8DF3F, 0xD8DF73,
        0xD8DFAB, 0xD8DFDA, 0x1D6DE20}) {
        image[rva] ^= 1;
        Check10(!HasVerifiedDeathMarkerLayout() && HasVerifiedWorldMapLayout(),
            "changed tombstone category, selector, consumer or diagnostic disables only death semantics");
        std::vector<unsigned char> ui(0x3E6A90), poi(0xA0), death(0xA0);
        Put10(poi.data(), 0x10, WorldToFixed(1000));Put10(poi.data(), 0x18, WorldToFixed(200));
        Put10(poi.data(), 0x20, WorldToFixed(1200));Put10(poi.data(), 0x30, std::uint32_t{WORLD_MAP_FLAME_ALTAR});
        Put10(death.data(), 0x10, WorldToFixed(1020));Put10(death.data(), 0x18, WorldToFixed(200));
        Put10(death.data(), 0x20, WorldToFixed(1000));Put10(death.data(), 0x30, std::uint32_t{0x1AD8F96E});
        Put10(ui.data(), 0x36EA00, reinterpret_cast<uintptr_t>(poi.data()));Put10(ui.data(), 0x36EA08, std::uint64_t{1});
        Put10(ui.data(), 0x3E6A78, reinterpret_cast<uintptr_t>(death.data()));Put10(ui.data(), 0x3E6A80, std::uint64_t{1});
        g_worldMapLayoutSupported=HasVerifiedWorldMapLayout();g_deathMarkerLayoutSupported=HasVerifiedDeathMarkerLayout();
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(ui.data()),100);
        const auto markers=CopyWorldMapMarkers(100);
        Check10(g_worldMapValid && markers.size()==1 && markers[0].key==WORLD_MAP_FLAME_ALTAR && CopyDeathMarkers(100).empty(),
            "death-only semantic rejection preserves real POI capture and suppresses death publication");
        image[rva] ^= 1;
        Check10(HasVerifiedDeathMarkerLayout(), "restored semantic evidence admits tombstones again");
    }
    ClearLiveMarkers();
    image[0x307104]^=1;Check10(!HasVerifiedLiveFogLayout() && HasVerifiedWorldClockLayout(),"changed fog call disables only fog");image[0x307104]^=1;
    image[0x2fdb78]^=1;Check10(!HasVerifiedWorldWeatherLayout() && HasVerifiedWorldMapLayout(),"changed weather call disables only weather");image[0x2fdb78]^=1;
    image[0x1bbb4e0]^=1;Check10(!HasVerifiedWorldWeatherLayout(),"changed weather enum is rejected");image[0x1bbb4e0]^=1;
    image[0x1e022a0]^=1;Check10(!NamedClient10(),"matching name with wrong camera function is rejected");image[0x1e022a0]^=1;
    CheckReaders10();
    CheckInactive10();
    CheckLifecycle10();
    CheckVulkanOwnership10();
}
