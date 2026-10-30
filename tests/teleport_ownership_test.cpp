// Focused production-TU fixture. Third-party images are inert RW/NX data.
#define wmain PublicCreativeLegacyMain
#include "public_creative_compat_test.cpp"
#undef wmain

namespace
{
    unsigned missingAdmissionBoundaryCalls=0;
    // Executable RED fallback only. The real dllmain MenuOwnershipSample&
    // overload supersedes this when the explicit load boundary is implemented.
    bool BeginCurrentMenuOwnershipAdmission(...)
    {
        ++missingAdmissionBoundaryCalls;
        PublicCreativeCompat::ForgetIdentity();
        MenuOwnershipSample sample{};
        return SampleCurrentMenuOwnership(sample,g_menuVisibilityCallsiteEverActivated.load(),
            PublicCreativeCompat::ProofOrigin::AdmissionUpdate);
    }
}

namespace TeleportTest
{
    using namespace PublicCreativeTest;
    constexpr uintptr_t TeleportCallback=0x2650,TeleportNext=0x694b8;
    constexpr uintptr_t FindCallback=0xaf40,FindNext=0x6f680;
    constexpr char TeleportHash[]="4c44b427737f75644223b00adbcf64c7dde51b542d5d40a830190dbd728c0b27";
    enum class Topology { Raw, Teleport, TeleportFind, FindTeleport, CreativeTeleport, CreativeTeleportFind, CreativeFindTeleport, CreativeFind };
    uintptr_t TeleRelay(){return Arena()+0x9000;}
    uintptr_t FindRelay(){return Arena()+0xa000;}
    uintptr_t Terminal(){return Arena()+0xb000;}
    Mem::Detour* retained=nullptr;
    unsigned admissionEpoch=0;

    void Counts()
    {
        backingFileOpenCalls=backingReadCalls=modulePathCalls=workingSetCalls=queryApiCalls=0;
        ownerWorkingSetCalls=ownerQueryCalls=0;
        backingReadBytes=0;hashCreateCalls=0;algorithmOpenCalls=0;algorithmCloseCalls=0;
        backingInfoCalls=0;
    }
    bool Admission()
    {
        ++admissionEpoch;
        MenuOwnershipSample sample{};
        return BeginCurrentMenuOwnershipAdmission(sample);
    }
    bool Sample(PublicCreativeCompat::ProofOrigin origin)
    {
        MenuOwnershipSample sample{};
        return SampleCurrentMenuOwnership(sample,retained!=nullptr,origin) && IsAdmitted(sample.admission);
    }
    void Relay(uintptr_t address,uintptr_t callback)
    {
        std::memset(reinterpret_cast<void*>(address),0,32);
        const std::uint8_t code[]={0xff,0x25,10,0,0,0};
        std::memcpy(reinterpret_cast<void*>(address),code,sizeof(code));Put(address+16,callback);
    }
    std::array<std::uint8_t,18> PrefixTo(uintptr_t relay)
    {
        std::array<std::uint8_t,18> value{};value.fill(0x90);
        value[0]=0x48;value[1]=0xb8;value[10]=0xff;value[11]=0xe0;
        std::memcpy(value.data()+2,&relay,8);return value;
    }
    void Chain(Topology topology)
    {
        Known(1);
        if(topology==Topology::Raw){Raw();return;}
        std::memset(reinterpret_cast<void*>(Arena()+0x9000),0,0x3000);
        std::memcpy(reinterpret_cast<void*>(Terminal()),Prefix,18);
        const std::uint8_t jump[]={0xff,0x25,0,0,0,0};
        std::memcpy(reinterpret_cast<void*>(Terminal()+18),jump,6);Put(Terminal()+24,A()+18);
        Relay(TeleRelay(),reinterpret_cast<uintptr_t>(teleportModule)+TeleportCallback);
        Relay(FindRelay(),reinterpret_cast<uintptr_t>(findModule)+FindCallback);
        const bool findFirst=topology==Topology::FindTeleport || topology==Topology::CreativeFindTeleport || topology==Topology::CreativeFind;
        const bool pair=topology==Topology::TeleportFind || topology==Topology::FindTeleport ||
            topology==Topology::CreativeTeleportFind || topology==Topology::CreativeFindTeleport;
        Put(reinterpret_cast<uintptr_t>(teleportModule)+TeleportNext,pair && !findFirst?FindRelay():Terminal());
        Put(reinterpret_cast<uintptr_t>(findModule)+FindNext,pair && findFirst?TeleRelay():Terminal());
        const auto underlying=PrefixTo(findFirst?FindRelay():TeleRelay());
        std::memcpy(reinterpret_cast<void*>(A()),underlying.data(),underlying.size());
        const bool creative=topology==Topology::CreativeTeleport || topology==Topology::CreativeTeleportFind ||
            topology==Topology::CreativeFindTeleport || topology==Topology::CreativeFind;
        if(!creative)return;
        std::memset(reinterpret_cast<void*>(A()),0x90,10);Rel32(A(),0xe9,T()+15);
        std::memset(reinterpret_cast<void*>(T()),0,32);
        std::memcpy(reinterpret_cast<void*>(T()),underlying.data(),10);Rel32(T()+10,0xe9,A()+10);
        std::memcpy(reinterpret_cast<void*>(T()+15),jump,6);Put(T()+21,B());
        std::memcpy(reinterpret_cast<void*>(Backup()),underlying.data(),10);
        Put(H()+0x28,uintptr_t{29});Put(H()+0x38,Backup()+10);Put(H()+0x40,Backup()+10);Put(H()+0x48,uintptr_t{29});
    }
    void Setup(int argc,wchar_t** argv)
    {
        Require(argc==6,"four explicit inert fixture inputs: game, Creative301, Find112, Teleport116");
        SelectCreative301Fixture();g_clientProfile=MinimapClient10::IdentifyFile(argv[2]);
        Require(g_clientProfile==MinimapClient10::Profile::Build26082781,"pinned current game profile");
        const auto gameFile=ReadFile(argv[2]),creative=ReadFile(argv[3]),find=ReadFile(argv[4]),teleport=ReadFile(argv[5]);
        Require(ImageSize(creative)==Creative301ImageSize && ImageSize(find)==FindImageSize &&
            ImageSize(teleport)==TeleportImageSize,"bounded authentic fixture image sizes");
        Require(teleport.size()==442368 && PublicCreativeCompat::HasHash(teleport.data(),teleport.size(),TeleportHash),
            "independent exact Teleport116 file pin");
        gameSize=ImageSize(gameFile);
        game=static_cast<std::uint8_t*>(::VirtualAlloc(nullptr,gameSize+0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        module=static_cast<std::uint8_t*>(::VirtualAlloc(nullptr,CreativeImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        findModule=static_cast<std::uint8_t*>(::VirtualAlloc(nullptr,FindImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        teleportModule=static_cast<std::uint8_t*>(::VirtualAlloc(nullptr,TeleportImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        Require(game && module && findModule && teleportModule,"four private RW/NX image allocations");
        MapData(gameFile,game,gameSize);MapData(creative,module,CreativeImageSize);
        MapData(find,findModule,FindImageSize);MapData(teleport,teleportModule,TeleportImageSize);
        ValidateCreative301Fixture(creative);
        Require(PublicCreativeCompat::HasHash(teleportModule+TeleportCallback,0x58,
            "295f683531243215b4c6a6245b08f433c2007481a32c98c1670b195aa59e12e4"),"independent Teleport callback-body pin");
        modulePath=argv[3];findModulePath=argv[4];teleportModulePath=argv[5];
        g_exeBase=reinterpret_cast<uintptr_t>(game);g_exeImageSize=gameSize;g_client10=true;
        Put(g_exeBase+ClientRva(MinimapMenu10::UiContextRva),Arena()+0x6000);
        Put(Arena()+0x6000+MinimapMenu10::HolderOffset,Arena()+0x7000);
        Put(Arena()+0x7000,Arena()+0x7800);Put(Arena()+0x7800+MinimapMenu10::PlayStateOffset,Arena()+0x8000);
        Put(Arena()+0x8000+MinimapMenu10::ScreenOffset,std::uint8_t{1});
        g_originalMenuRenderer.store(A());
        // Preload the heap/protection metadata path before deterministic counts.
        std::uint8_t touch[18]{};PublicCreativeCompat::Copy(A(),touch,sizeof(touch),true);
    }
    void Finish()
    {
        onWorkingPage={};onBackingRead={};duringOriginal={};denyBackingFileOpens=false;
        if(retained){DestroyCall(retained);retained=nullptr;}
        PublicCreativeCompat::ForgetIdentity();
        Check(moduleAcquires==moduleReleases,"all temporary provider module references balanced");
        ::VirtualFree(game,0,MEM_RELEASE);::VirtualFree(module,0,MEM_RELEASE);
        ::VirtualFree(findModule,0,MEM_RELEASE);::VirtualFree(teleportModule,0,MEM_RELEASE);
        game=module=findModule=teleportModule=nullptr;g_exeBase=0;g_exeImageSize=0;g_client10=false;
        std::cout<<"cases="<<cases<<" failed="<<failed<<'\n';
    }
    void OwnershipCases()
    {
        for(auto topology:{Topology::Teleport,Topology::TeleportFind,Topology::FindTeleport,
            Topology::CreativeTeleport,Topology::CreativeTeleportFind,Topology::CreativeFindTeleport,Topology::CreativeFind})
        {
            Chain(topology);const bool admitted=Admission();
            std::cout<<"TOPOLOGY "<<static_cast<unsigned>(topology)<<" admitted="<<admitted<<'\n';
            Check(admitted,"explicit dllmain admission accepts the exact observed bounded provider topology");
            if(!retained)retained=RetainCall();
            CaptureCase("exact provider chain captures gameplay only after original",true);
            Check(Sample(PublicCreativeCompat::ProofOrigin::CallbackPre) && Sample(PublicCreativeCompat::ProofOrigin::CallbackPost),
                "independent warm pre/post proof accepts stable exact provider chain");
        }
        Check(missingAdmissionBoundaryCalls==0,"actual dllmain explicit admission boundary is implemented and exercised");
        const auto coldReject=[&](const char* name,auto mutate){Chain(Topology::CreativeTeleportFind);mutate();Counts();
            Check(!Admission() && backingFileOpenCalls==0 && backingReadCalls==0 && modulePathCalls==0,name);};
        coldReject("unknown cold topology spends no provider or Creative file attempt",[]{Put(TeleRelay()+16,Arena()+0x5000);});
        coldReject("cyclic cold topology spends no provider or Creative file attempt",[]{Put(reinterpret_cast<uintptr_t>(findModule)+FindNext,TeleRelay());});
        coldReject("duplicate cold topology spends no provider or Creative file attempt",[]{Put(FindRelay()+16,reinterpret_cast<uintptr_t>(teleportModule)+TeleportCallback);});
        const auto reject=[&](const char* name,auto mutate){Chain(Topology::CreativeTeleportFind);Admission();mutate();
            Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre),name);};
        reject("unknown callback owner fails closed",[]{Put(TeleRelay()+16,Arena()+0x5000);});
        reject("wrong exact callback RVA fails closed",[]{Put(TeleRelay()+16,reinterpret_cast<uintptr_t>(teleportModule)+TeleportCallback+1);});
        reject("two-provider cycle fails closed",[]{Put(reinterpret_cast<uintptr_t>(findModule)+FindNext,TeleRelay());});
        reject("duplicate provider fails closed",[]{Put(FindRelay()+16,reinterpret_cast<uintptr_t>(teleportModule)+TeleportCallback);});
        reject("nonzero relay padding fails closed",[]{Put(TeleRelay()+8,std::uint8_t{1});});
        reject("changed original eighteenth byte fails closed",[]{Put(Terminal()+17,std::uint8_t{1});});
        reject("wrong terminal return fails closed",[]{Put(Terminal()+24,A()+17);});
        reject("unreadable continuation fails closed",[]{Put(reinterpret_cast<uintptr_t>(teleportModule)+TeleportNext,uintptr_t{1});});
        reject("Teleport over detached Creative remains unsupported",[]{Put(reinterpret_cast<uintptr_t>(teleportModule)+TeleportNext,B());});
        Chain(Topology::CreativeTeleport);Admission();
        const auto body=teleportModule[TeleportCallback];teleportModule[TeleportCallback]^=1;
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre),"warm file identity cannot admit changed callback bytes");
        teleportModule[TeleportCallback]=body;
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPost),"restoring observed changed callback bytes cannot resurrect approval");
        Chain(Topology::CreativeTeleport);Admission();
        CaptureCase("native-interval continuation mutation clears staged gameplay",false,[]{Put(reinterpret_cast<uintptr_t>(teleportModule)+TeleportNext,uintptr_t{1});});
        Chain(Topology::CreativeTeleportFind);Admission();
        unsigned slotVisits=0;
        const auto slotPage=(reinterpret_cast<uintptr_t>(teleportModule)+TeleportNext)&~uintptr_t(4095);
        onWorkingPage=[&](uintptr_t page){if(page==slotPage && ++slotVisits==2)Put(reinterpret_cast<uintptr_t>(teleportModule)+TeleportNext,Terminal()+1);};
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPost),"mutation between owned chain snapshots rejects admission");
        onWorkingPage={};
        Chain(Topology::Teleport);Check(Admission(),"identity invalidation control primes exact Teleport");
        const auto headerByte=teleportModule[0x20];teleportModule[0x20]^=1;
        Counts();Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre) && backingFileOpenCalls==0,
            "changed approved mapped header rejects warm identity without cold work");
        teleportModule[0x20]=headerByte;
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPost),"observed header loss remains invalidated after bytes are restored");
        Chain(Topology::Teleport);Check(Admission(),"new epoch recovers restored mapped identity");
        teleportModuleAvailable=false;
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre),"fresh module reference loss invalidates approval");
        teleportModuleAvailable=true;
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPost),"module reference restoration does not replenish an invalidated identity");
        Chain(Topology::Teleport);Check(Admission(),"new epoch recovers restored module ownership");
        hashCreateCalls=0;onHashCreate=[](unsigned){PublicCreativeCompat::ForgetIdentity();};
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre),"invalidation during final aggregate hash cannot publish stale proof");
        onHashCreate={};
    }
    void CacheCases()
    {
        countOwnerPhases=true;
        constexpr unsigned count=10000;
        Chain(Topology::CreativeFind);PublicCreativeCompat::ForgetIdentity();Counts();denyBackingFileOpens=true;
        bool rejected=true;
        for(unsigned i=0;i<count;++i){const bool pre=Sample(PublicCreativeCompat::ProofOrigin::CallbackPre);
            const bool post=Sample(PublicCreativeCompat::ProofOrigin::CallbackPost);rejected=!pre && !post && rejected;}
        Check(rejected && backingFileOpenCalls==0 && backingReadCalls==0 && modulePathCalls==0 && hashCreateCalls==0,
            "10k uncached callback pre/post samples reject with zero backing-file opens or reads");
        Chain(Topology::CreativeFind);Counts();
        Check(!Admission(),"explicit cold admission fails closed on backing-file failure");
        const auto firstAttempts=backingFileOpenCalls.load();const auto firstReads=backingReadCalls.load();
        for(unsigned i=0;i<count;++i)(void)Sample(PublicCreativeCompat::ProofOrigin::AdmissionUpdate);
        Check(firstAttempts<=1 && backingFileOpenCalls==firstAttempts && backingReadCalls==firstReads,
            "10k unchanged rejected admissions do not reopen or rehash their backing file");
        Raw();(void)Sample(PublicCreativeCompat::ProofOrigin::AdmissionUpdate);Chain(Topology::CreativeFind);
        (void)Sample(PublicCreativeCompat::ProofOrigin::AdmissionUpdate);
        Check(backingFileOpenCalls==firstAttempts,"raw fallback does not replenish admission cold budget");
        denyBackingFileOpens=false;
        Check(Admission(),"explicit new admission epoch can verify after a previous failed identity");
        // Removing negative memoization or moving permit consumption after I/O
        // would repeat one of these failures at every explicit activation.
        for(unsigned fault=0;fault<6;++fault)
        {
            Chain(Topology::Teleport);Counts();
            denyBackingFileOpens=fault==0;wrongBackingSize=fault==1;corruptBackingRead=fault==2;
            denyBackingReads=fault==3;unstableBackingMetadata=fault==4;denyModulePath=fault==5;
            Check(!Admission(),"every cold file/path/size/digest/read/metadata failure rejects admission");
            const auto opens=backingFileOpenCalls.load(),reads=backingReadCalls.load(),hashes=hashCreateCalls.load(),paths=modulePathCalls.load();
            Counts();bool allRejected=true;
            {PublicCreativeCompat::ContinueAdmissionScope sameEpoch;
                for(unsigned i=0;i<count;++i){const bool update=Sample(PublicCreativeCompat::ProofOrigin::AdmissionUpdate);
                    const bool pre=Sample(PublicCreativeCompat::ProofOrigin::CallbackPre),post=Sample(PublicCreativeCompat::ProofOrigin::CallbackPost);
                    allRejected=!update && !pre && !post && allRejected;}}
            std::cout<<"DENIAL fault="<<fault<<" first_opens="<<opens<<" first_reads="<<reads<<" first_hashes="<<hashes<<" first_paths="<<paths<<'\n';
            Check(allRejected && opens<=(fault==5?0u:1u) && paths==1 && reads<=7 &&
                backingFileOpenCalls==0 && backingReadCalls==0 && hashCreateCalls==0 && modulePathCalls==0,
                "10k same-epoch activations and callback pairs retain every negative outcome without cold work");
            Check(ownerWorkingSetCalls<=count*3*4 && ownerQueryCalls==0,
                "cached denial uses at most four owner copies per sample and no resident VirtualQuery");
            denyBackingFileOpens=wrongBackingSize=corruptBackingRead=denyBackingReads=unstableBackingMetadata=denyModulePath=false;
            Check(Admission(),"only genuine new Load epoch can recover a rejected provider");
        }
        for(auto topology:{Topology::Teleport,Topology::TeleportFind,Topology::CreativeTeleportFind})
        {
            Chain(topology);Counts();const bool prime=Admission();
            if(topology!=Topology::CreativeTeleportFind)
                Check(prime && backingFileOpenCalls==(topology==Topology::Teleport?1u:2u) &&
                    backingReadCalls<=(topology==Topology::Teleport?7u:15u) &&
                    backingReadBytes==(topology==Topology::Teleport?442368u:914944u),
                    "one genuine admission stays within exact single/pair cold file budget");
            Counts();denyBackingFileOpens=true;
            bool stable=prime;const auto begin=Clock();
            for(unsigned i=0;i<count;++i)stable=Sample(PublicCreativeCompat::ProofOrigin::CallbackPre) &&
                Sample(PublicCreativeCompat::ProofOrigin::CallbackPost) && stable;
            const auto elapsed=Clock()-begin;
            std::cout<<"CACHE topology="<<static_cast<unsigned>(topology)<<" iterations="<<count
                <<" ticks="<<elapsed<<" opens="<<backingFileOpenCalls<<" reads="<<backingReadCalls
                <<" hashes="<<hashCreateCalls<<" ws="<<workingSetCalls<<" vq="<<queryApiCalls<<'\n';
            std::cout<<"OWNER ws="<<ownerWorkingSetCalls<<" vq="<<ownerQueryCalls<<'\n';
            Check(stable && backingFileOpenCalls==0 && backingReadCalls==0 && modulePathCalls==0 && algorithmOpenCalls==0,
                "10k warm exact-provider callback pairs use fresh proof with no backing-file work");
            const unsigned ceiling=topology==Topology::CreativeTeleportFind?46u:24u;
            Check(ownerWorkingSetCalls<=count*2*ceiling && ownerQueryCalls==0,
                "warm proof respects explicit bounded read and fallback query ceilings");
            Check(hashCreateCalls==count*2,"each ownership proof uses only its single aggregate hash context");
            Counts();forceWorkingSetFallback=true;
            const bool fallback=Sample(PublicCreativeCompat::ProofOrigin::CallbackPre) && Sample(PublicCreativeCompat::ProofOrigin::CallbackPost);
            forceWorkingSetFallback=false;
            Check(fallback && ownerWorkingSetCalls<=2*ceiling && ownerQueryCalls<=4*ceiling &&
                backingFileOpenCalls==0 && backingReadCalls==0 && modulePathCalls==0,
                "fallback pre/post ownership proofs preserve Copy and VirtualQuery ceilings");
            if(!retained)retained=RetainCall();
            Counts();CaptureCase("warm wrapper keeps native forwarding and fresh owner proofs",true);
            Check(ownerWorkingSetCalls<=2*ceiling && ownerQueryCalls==0 && backingFileOpenCalls==0 &&
                backingReadCalls==0 && modulePathCalls==0,"native wrapper owner phases stay within48 or92 copies");
            Counts();forceWorkingSetFallback=true;
            CaptureCase("fallback wrapper preserves native forwarding and owner proofs",true);
            forceWorkingSetFallback=false;
            Check(ownerWorkingSetCalls<=2*ceiling && ownerQueryCalls<=4*ceiling,
                "native wrapper owner fallback stays within96 or184 VirtualQuery calls");
            denyBackingFileOpens=false;
        }
        Chain(Topology::CreativeFind);Admission();Counts();Sample(PublicCreativeCompat::ProofOrigin::CallbackPre);
        const auto baselineHashes=hashCreateCalls.load();
        Chain(Topology::CreativeTeleportFind);Admission();Counts();Sample(PublicCreativeCompat::ProofOrigin::CallbackPre);
        Check(hashCreateCalls<=baselineHashes,"second known provider does not add warm per-provider crypto contexts");
        Chain(Topology::CreativeTeleportFind);Check(Admission(),"lifecycle control primes exact identity once");
        // Chain's provider arena clear [0x9000,0xc000) also covers the
        // fixture screen at 0x8000+0x1cd8. Restore actual gameplay input
        // before resetting only its captured observation, as ResetCapture
        // does for the earlier wrapper cases. Do not reset after world leave.
        Put(Arena()+0x8000+MinimapMenu10::ScreenOffset,std::uint8_t{1});
        std::uint8_t lifecycleScreen=0;
        Check(TryReadCurrentHudScreenType(lifecycleScreen) && lifecycleScreen==1,
            "lifecycle fixture supplies an actual valid gameplay HUD screen before observation reset");
        Counts();denyBackingFileOpens=true;
        g_menuVisibilitySnapshot.Capture(1,GetTickCount64(),true);
        Check(g_menuVisibilitySnapshot.AllowsGameplay(GetTickCount64()),"lifecycle control starts with a captured gameplay observation");
        ClearMenuVisibilityObservation();
        Check(!g_menuVisibilitySnapshot.AllowsGameplay(GetTickCount64()),
            "world/session observation reset clears captured HUD state");
        Check(Sample(PublicCreativeCompat::ProofOrigin::CallbackPre) && Sample(PublicCreativeCompat::ProofOrigin::CallbackPost) &&
            backingFileOpenCalls==0 && backingReadCalls==0,
            "normal world leave/rejoin retains identity but freshly proves the chain without I/O");
        Check(!g_menuVisibilitySnapshot.AllowsGameplay(GetTickCount64()),
            "warm ownership proof alone cannot restore HUD visibility after session reset");
        // Do not use CaptureCase/ResetCapture here: exercise the cleared state
        // above through the actual next wrapper and inert native dispatch seam.
        nativeCalls=0;nativeAddressCorrect=false;visibleInsideOriginal=false;duringOriginal={};
        CaptureMenuVisibilityCallsiteHook();
        Check(nativeCalls==1 && nativeAddressCorrect && !visibleInsideOriginal,
            "post-reset wrapper forwards once and keeps fresh HUD observation unpublished during native rendering");
        Check(g_menuVisibilitySnapshot.AllowsGameplay(GetTickCount64()) && backingFileOpenCalls==0 &&
            backingReadCalls==0 && modulePathCalls==0,
            "post-reset valid HUD callback publishes fresh gameplay visibility using warm identity without I/O");
        Put(reinterpret_cast<uintptr_t>(teleportModule)+TeleportNext,Terminal()+1);
        Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre) && backingFileOpenCalls==0,
            "fresh continuation validation still rejects a changed chain after HUD reset");
        // A renderer owner loaded after Minimap Load can use an unused attempt
        // at the real explicit activation boundary, never through Update.
        denyBackingFileOpens=false;Chain(Topology::Raw);Check(Admission(),"raw Load begins a genuine ownership epoch");
        if(!retained)retained=RetainCall();
        Chain(Topology::Teleport);Counts();
        Check(!Sample(PublicCreativeCompat::ProofOrigin::AdmissionUpdate) && backingFileOpenCalls==0,
            "late uncached provider cannot start cold verification from Update");
        ModContext context{};bool pending=true;
        Check(ContinueMenuOwnershipAdmissionAtActivation(&context,pending) && !pending && backingFileOpenCalls==1,
            "explicit Activate spends the late provider's remaining Load-epoch attempt");
        const auto attempts=backingFileOpenCalls.load();ClearMenuVisibilityObservation();
        Check(Sample(PublicCreativeCompat::ProofOrigin::CallbackPre) && backingFileOpenCalls==attempts,
            "late provider stays warm after a later HUD/session reset");
        PublicCreativeCompat::ForgetIdentity();
        Check(!ContinueMenuOwnershipAdmissionAtActivation(&context,pending) && backingFileOpenCalls==attempts,
            "repeated activation cannot renew invalidated provider admission budget");
        countOwnerPhases=false;
    }
    void CostCases()
    {
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        const auto print=[&](const char* phase,Topology topology,std::vector<unsigned long long>& ticks){
            std::sort(ticks.begin(),ticks.end());
            std::cout<<phase<<" topology="<<static_cast<unsigned>(topology)<<" n="<<ticks.size()
                <<" p50_us="<<ticks[ticks.size()/2]*1000000.0/frequency.QuadPart
                <<" p95_us="<<ticks[(ticks.size()*95)/100]*1000000.0/frequency.QuadPart
                <<" max_us="<<ticks.back()*1000000.0/frequency.QuadPart
                <<" opens="<<backingFileOpenCalls<<" reads="<<backingReadCalls<<" bytes="<<backingReadBytes
                <<" paths="<<modulePathCalls<<" hashes="<<hashCreateCalls
                <<" ws="<<workingSetCalls<<" vq="<<queryApiCalls<<'\n';};
        for(auto topology:{Topology::Raw,Topology::CreativeFind,Topology::Teleport,Topology::TeleportFind,Topology::CreativeTeleportFind})
        {
            Chain(topology);Counts();std::vector<unsigned long long> coldTicks;
            bool accepted=true;
            for(unsigned i=0;i<8;++i){const auto start=Clock();const bool ok=Admission();
                coldTicks.push_back(Clock()-start);accepted=ok && accepted;}
            print("COLD",topology,coldTicks);Check(accepted,"cost topology is accepted after explicit cold admission");
            if(!accepted)continue;
            std::vector<unsigned long long> ticks;Counts();
            for(unsigned i=0;i<256;++i){const auto start=Clock();const bool ok=Sample(PublicCreativeCompat::ProofOrigin::CallbackPre);
                ticks.push_back(Clock()-start);Require(ok,"cost samples remain admitted");}
            print("COST",topology,ticks);
            Check(backingFileOpenCalls==0 && backingReadCalls==0 && modulePathCalls==0 && algorithmOpenCalls==0,
                "warm QPC measurement performs no cold or path work");
            // Accepted raw/CreativeFind baselines only. Pair topology is new
            // support and has no prior accepted rendering speedup baseline.
            const double p95=ticks[243]*1000000.0/frequency.QuadPart;
            if(topology==Topology::Raw)Check(p95<=4.8,"raw ownership p95 stays within approved4.8us ceiling");
            if(topology==Topology::CreativeFind)Check(p95<=31.0,"Creative-over-Find ownership p95 stays within approved31us ceiling");
        }
        Chain(Topology::Teleport);denyBackingFileOpens=true;
        Check(!Admission(),"rejection cost begins from a memoized failed cold attempt");
        Counts();std::vector<unsigned long long> rejectedTicks;
        for(unsigned i=0;i<256;++i){const auto start=Clock();const bool accepted=Sample(PublicCreativeCompat::ProofOrigin::CallbackPre);
            rejectedTicks.push_back(Clock()-start);Require(!accepted,"rejection cost remains fail closed");}
        print("REJECT",Topology::Teleport,rejectedTicks);
        Check(backingFileOpenCalls==0 && backingReadCalls==0 && modulePathCalls==0 && hashCreateCalls==0,
            "rejection QPC measurement performs no cold, path or hash work");
        denyBackingFileOpens=false;
    }
    void ContentionCases()
    {
        // The admission thread is deliberately paused INSIDE its first backing
        // read. A synthetic callback/invalidation runs on another fixture thread.
        // The pause is bounded so legacy RED cannot deadlock the test process.
        for(unsigned mode:{0u,1u,2u})
        {
            const bool invalidate=mode==1;
            Chain(Topology::Teleport);Counts();
            std::atomic<bool> probed{false},started{false},finished{false};
            bool finishedBeforeIoReturned=false,callbackRejected=false;
            std::thread worker;
            onBackingRead=[&]{
                if(probed.exchange(true))return;
                worker=std::thread([&]{started=true;
                    if(invalidate)PublicCreativeCompat::ForgetIdentity();
                    else if(mode==2){PublicCreativeCompat::ContinueAdmissionScope sameEpoch;
                        callbackRejected=!Sample(PublicCreativeCompat::ProofOrigin::AdmissionUpdate);}
                    else callbackRejected=!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre);
                    finished=true;
                });
                const auto deadline=::GetTickCount64()+1000;
                while(!started && ::GetTickCount64()<deadline)::Sleep(1);
                const auto finishDeadline=::GetTickCount64()+250;
                while(!finished && ::GetTickCount64()<finishDeadline)::Sleep(1);
                finishedBeforeIoReturned=finished.load();
            };
            const bool admitted=Admission();
            if(worker.joinable())worker.join();
            onBackingRead={};
            Check(probed && started,"cold reservation test reaches the real backing-read seam");
            Check(finishedBeforeIoReturned,invalidate?
                "invalidation does not wait behind admission file I/O or hashing":
                mode==2?"concurrent admission fails promptly without duplicating the reserved cold attempt":
                "warm-only callback fails promptly while admission file I/O is paused");
            if(invalidate)Check(!admitted,"cold completion cannot publish approval after reservation invalidation");
            else Check(callbackRejected,"in-progress identity cannot become a callback positive or recursive cold attempt");
            Check(backingFileOpenCalls==1 && backingReadCalls<=7,"concurrent work cannot duplicate the reserved Teleport file attempt");
            if(invalidate)
            {
                Counts();Check(!Sample(PublicCreativeCompat::ProofOrigin::CallbackPost) && backingFileOpenCalls==0,
                    "stale cold completion leaves no callback approval");
                Check(Admission(),"genuine later epoch can recover after in-progress invalidation");
            }
        }
        Chain(Topology::CreativeFind);Check(Admission(),"metadata contention control primes an approved identity");
        std::atomic<bool> locked{false},release{false},expired{false};
        std::thread holder([&]{std::lock_guard<std::mutex> lock(PublicCreativeCompat::fileCache.mutex);
            locked=true;const auto deadline=::GetTickCount64()+250;
            while(!release && ::GetTickCount64()<deadline)::Sleep(1);
            expired=!release.load();});
        while(!locked)::Sleep(1);
        Counts();
        const bool rejected=!Sample(PublicCreativeCompat::ProofOrigin::CallbackPre);
        release=true;holder.join();
        Check(rejected && !expired && backingFileOpenCalls==0 && backingReadCalls==0,
            "callback fails closed on a contended short metadata lock without waiting or I/O");
    }
}

int wmain(int argc,wchar_t** argv)
{
    using namespace TeleportTest;
    if(argc!=6){std::cerr<<"usage: teleport_ownership_test --teleport-owners|--cache-policy|--cache-contention|--teleport-cost GAME CREATIVE301 FIND112 TELEPORT116\n";return 2;}
    Setup(argc,argv);
    const std::wstring selector=argv[1];
    if(selector==L"--teleport-owners")OwnershipCases();
    else if(selector==L"--cache-policy")CacheCases();
    else if(selector==L"--teleport-cost")CostCases();
    else if(selector==L"--cache-contention")ContentionCases();
    else {std::cerr<<"unknown selector\n";return 2;}
    Finish();return failed?1:0;
}
