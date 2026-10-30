// Included inside PublicCreativeCompat. Cold work is explicitly admitted once;
// callbacks only copy owned approvals under a bounded metadata try-lock.
struct TrustedImageBytes
{
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char,BodySize> body{};
    std::array<unsigned char,BlockSize> pattern{};
    std::array<unsigned char,5> thunk{};
};
struct IdentityStamp
{
    std::uint64_t epoch=0,invalidation=0;
};
inline std::atomic<std::uint64_t> ownershipEpoch{1},ownershipInvalidation{1};
inline thread_local std::uint64_t coldAdmissionEpoch=0;
inline thread_local ProofOrigin identityOrigin=ProofOrigin::Other;
inline IdentityStamp CurrentIdentityStamp()
{return {ownershipEpoch.load(std::memory_order_acquire),ownershipInvalidation.load(std::memory_order_acquire)};}
inline bool IdentityStampValid(const IdentityStamp& value)
{const auto now=CurrentIdentityStamp();return value.epoch==now.epoch && value.invalidation==now.invalidation;}
class AdmissionEpochScope
{
    std::uint64_t prior;
public:
    AdmissionEpochScope():prior(coldAdmissionEpoch)
    {coldAdmissionEpoch=ownershipEpoch.fetch_add(1,std::memory_order_acq_rel)+1;ownershipInvalidation.fetch_add(1,std::memory_order_acq_rel);}
    ~AdmissionEpochScope(){coldAdmissionEpoch=prior;}
};
class IdentityOriginScope
{
    ProofOrigin priorOrigin;std::uint64_t priorPermit;
public:
    explicit IdentityOriginScope(ProofOrigin origin):priorOrigin(identityOrigin),priorPermit(coldAdmissionEpoch)
    {identityOrigin=origin;if(origin==ProofOrigin::CallbackPre || origin==ProofOrigin::CallbackPost)coldAdmissionEpoch=0;}
    ~IdentityOriginScope(){identityOrigin=priorOrigin;coldAdmissionEpoch=priorPermit;}
};
// The loader's explicit Activate boundary can spend an unused Load-epoch
// attempt. It never replenishes a rejected/in-progress provider's budget.
class ContinueAdmissionScope
{
    std::uint64_t prior;
public:
    ContinueAdmissionScope():prior(coldAdmissionEpoch){coldAdmissionEpoch=ownershipEpoch.load(std::memory_order_acquire);}
    ~ContinueAdmissionScope(){coldAdmissionEpoch=prior;}
};
enum class IdentityState { Unseen, InProgress, Approved, Rejected };
struct FileCache
{
    std::mutex mutex;
    std::uint64_t epoch=0,invalidation=0,reservation=0;
    std::uintptr_t opaqueKey=0;
    bool attempted=false,trusted=false;
    IdentityState state=IdentityState::Unseen;
    const Profile* profile=nullptr; // owned constexpr descriptor only
    std::wstring path;
    BY_HANDLE_FILE_INFORMATION info{};
    Digest imageIdentity{};
    TrustedImageBytes expected{};
};
inline FileCache fileCache,findFileCache,teleportFileCache;
// Invalidation never waits for a cold reader and never replenishes its budget.
inline void ForgetFindIdentity(){ownershipInvalidation.fetch_add(1,std::memory_order_acq_rel);}
inline void ForgetIdentity(){ownershipInvalidation.fetch_add(1,std::memory_order_acq_rel);}
inline bool SameFile(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b)
{
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber && a.nFileIndexHigh==b.nFileIndexHigh &&
        a.nFileIndexLow==b.nFileIndexLow && a.nFileSizeHigh==b.nFileSizeHigh && a.nFileSizeLow==b.nFileSizeLow &&
        a.ftLastWriteTime.dwHighDateTime==b.ftLastWriteTime.dwHighDateTime &&
        a.ftLastWriteTime.dwLowDateTime==b.ftLastWriteTime.dwLowDateTime;
}
// Keep the large file/path buffers out of every warm callback stack frame.
__declspec(noinline) inline bool ColdFileIdentity(HMODULE module,const Profile& profile,std::wstring& path,
    BY_HANDLE_FILE_INFORMATION& info,Digest& identity,TrustedImageBytes& expected)
{
    wchar_t name[32768]{};
    const DWORD length=GetModuleFileNameW(module,name,static_cast<DWORD>(std::size(name)));
    if(!length || length>=std::size(name))return false;
    path.assign(name,length);
#if ESM_AGGREGATE_TIMING_DIAGNOSTICS
    auto& collector=MinimapAggregateTiming::DiagnosticCollector();
#endif
    MINIMAP_PCC_AGG_COUNT(collector,MinimapAggregateTiming::Metric::BackingFileOpenAttempt);
    const HANDLE file=CreateFileW(name,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)
    {MINIMAP_PCC_AGG_COUNT(collector,MinimapAggregateTiming::Metric::BackingFileOpenFailure);return false;}
    MINIMAP_PCC_AGG_COUNT(collector,MinimapAggregateTiming::Metric::BackingFileOpenSuccess);
    struct FileReference{HANDLE value;~FileReference(){CloseHandle(value);}} reference{file};
    BY_HANDLE_FILE_INFORMATION before{},after{};
    if(!GetFileInformationByHandle(file,&before) || before.nFileSizeHigh || before.nFileSizeLow!=profile.fileSize)return false;
    Hash hash;std::array<unsigned char,65536> buffer{};std::size_t total=0;
    while(total<profile.fileSize)
    {
        DWORD read=0;const DWORD wanted=static_cast<DWORD>((std::min)(profile.fileSize-total,buffer.size()));
#if ESM_AGGREGATE_TIMING_DIAGNOSTICS
        std::uint64_t start=0,end=0;const bool clock=ReadProofQpc(start);
#endif
        const BOOL ok=ReadFile(file,buffer.data(),wanted,&read,nullptr);
#if ESM_AGGREGATE_TIMING_DIAGNOSTICS
        if(clock && ReadProofQpc(end) && end>=start)MINIMAP_PCC_AGG_RECORD(collector,MinimapAggregateTiming::Metric::BackingFileRead,end-start,read);
        else MINIMAP_PCC_AGG_TIMING_FAILURE(collector,MinimapAggregateTiming::Metric::BackingFileRead);
#endif
        if(!ok || read!=wanted || !hash.Add(buffer.data(),read))return false;
        total+=read;
    }
    Digest digest{};
    if(!hash.Finish(digest) || !HexEquals(digest,profile.fileHash) ||
        !GetFileInformationByHandle(file,&after) || !SameFile(before,after))return false;
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    if(!Copy(base,&expected.dos,sizeof(expected.dos)) || expected.dos.e_magic!=IMAGE_DOS_SIGNATURE ||
        expected.dos.e_lfanew<=0 || static_cast<std::size_t>(expected.dos.e_lfanew)>4096-sizeof(expected.nt) ||
        !Copy(base+expected.dos.e_lfanew,&expected.nt,sizeof(expected.nt)) || expected.nt.Signature!=IMAGE_NT_SIGNATURE ||
        expected.nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || expected.nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        expected.nt.OptionalHeader.SizeOfImage!=profile.imageSize ||
        !Copy(base+profile.body,expected.body.data(),profile.bodySize,true) ||
        !HasHash(expected.body.data(),profile.bodySize,profile.bodyHash))return false;
    if(profile.pattern && (!Copy(base+profile.pattern,expected.pattern.data(),expected.pattern.size()) ||
        !HasHash(expected.pattern.data(),expected.pattern.size(),TemplateHash) ||
        !Copy(base+profile.callback,expected.thunk.data(),expected.thunk.size(),true) || expected.thunk!=profile.thunk))return false;
    identity=digest;info=after;return true;
}
inline bool FileIdentity(HMODULE module,Digest& identity,const Profile*& profile,
    FileCache& cache=fileCache,const Profile* required=nullptr,TrustedImageBytes* expected=nullptr,
    bool deferCold=false,bool* deferred=nullptr)
{
    profile=nullptr;identity={};
    if(deferred)*deferred=false;
    if(!module || !required)return false;
    // Fresh referenced module ownership and approved mapped bytes establish
    // affinity. Path discovery belongs only to the consumed cold attempt.
    // Unobserved same-base unload/reload ABA still requires a process restart.
    const auto stamp=CurrentIdentityStamp();
    // A collision-free opaque equality tag, never dereferenced or used as trust.
    const auto key=reinterpret_cast<std::uintptr_t>(module)^reinterpret_cast<std::uintptr_t>(&ownershipEpoch);
    std::uint64_t reservation=0;
    {
        std::unique_lock<std::mutex> lock(cache.mutex,std::try_to_lock);
        if(!lock || !IdentityStampValid(stamp))return false;
        const bool cold=coldAdmissionEpoch==stamp.epoch && identityOrigin!=ProofOrigin::CallbackPre && identityOrigin!=ProofOrigin::CallbackPost;
        if(cache.epoch!=stamp.epoch)
        {
            if(!cold)return false;
            cache.epoch=stamp.epoch;cache.invalidation=stamp.invalidation;cache.attempted=false;cache.trusted=false;
            cache.state=IdentityState::Unseen;cache.profile=required;cache.opaqueKey=key;++cache.reservation;
        }
        if(cache.profile!=required || cache.opaqueKey!=key || cache.invalidation!=stamp.invalidation)
        {
            if(cache.profile!=required || cache.opaqueKey!=key)ForgetIdentity();
            cache.trusted=false;cache.state=IdentityState::Rejected;++cache.reservation;
            return false; // observed replacement/invalidation requires an explicit new epoch
        }
        if(cache.state==IdentityState::Approved && cache.trusted)
        {
            identity=cache.imageIdentity;profile=cache.profile;if(expected)*expected=cache.expected;
#if ESM_AGGREGATE_TIMING_DIAGNOSTICS
            MINIMAP_PCC_AGG_COUNT(MinimapAggregateTiming::DiagnosticCollector(),MinimapAggregateTiming::Metric::ProofCacheWarmHit);
#endif
            return IdentityStampValid(stamp);
        }
        if(!cold || cache.attempted || cache.state!=IdentityState::Unseen)return false;
        // A chain first validates its bounded topology without file work.
        if(deferCold){if(deferred)*deferred=true;return false;}
        cache.attempted=true;cache.state=IdentityState::InProgress;reservation=++cache.reservation;
        MINIMAP_PCC_AGG_COUNT(MinimapAggregateTiming::DiagnosticCollector(),MinimapAggregateTiming::Metric::ProofCacheColdAttempt);
    }
    // No cache lock, callback wait, or partial approved payload exists during I/O.
    std::wstring path;BY_HANDLE_FILE_INFORMATION info{};Digest result{};TrustedImageBytes bytes{};
#if ESM_AGGREGATE_TIMING_DIAGNOSTICS
    struct ColdOutcome
    {
        bool success=false;
        ~ColdOutcome(){MinimapAggregateTiming::DiagnosticCollector().Count(success?
            MinimapAggregateTiming::Metric::ProofColdSuccess:MinimapAggregateTiming::Metric::ProofColdFailure);}
    } outcome;
#endif
    bool accepted=false;
    try{accepted=ColdFileIdentity(module,*required,path,info,result,bytes);}
    catch(...){accepted=false;} // Allocation/path failures are terminal attempts too.
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        if(!IdentityStampValid(stamp) || cache.epoch!=stamp.epoch || cache.invalidation!=stamp.invalidation ||
            cache.opaqueKey!=key || cache.profile!=required || cache.reservation!=reservation || cache.state!=IdentityState::InProgress)return false;
        cache.state=accepted?IdentityState::Approved:IdentityState::Rejected;cache.trusted=accepted;
        if(!accepted)return false;
        cache.path=std::move(path);cache.info=info;cache.imageIdentity=result;cache.expected=bytes;
        identity=result;profile=required;if(expected)*expected=bytes;
#if ESM_AGGREGATE_TIMING_DIAGNOSTICS
        outcome.success=true;
#endif
    }
    return IdentityStampValid(stamp);
}
