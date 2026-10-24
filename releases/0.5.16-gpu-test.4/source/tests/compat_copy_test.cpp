// Real Windows protection checks on owned data pages only. No foreign code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <array>
#include <cstring>
namespace Probe {
    unsigned queries=0,workingQueries=0;
    bool failWorking=false, invalidWorking=false, invalidTail=false;
    DWORD lastRows=0; std::uintptr_t firstFallback=0;
    SIZE_T WINAPI Query(LPCVOID p, PMEMORY_BASIC_INFORMATION m, SIZE_T n) {
        if(!queries)firstFallback=reinterpret_cast<std::uintptr_t>(p);
        ++queries; return ::VirtualQuery(p,m,n);
    }
    BOOL WINAPI Working(HANDLE h,PVOID p,DWORD n) {
        ++workingQueries; lastRows=n/sizeof(PSAPI_WORKING_SET_EX_INFORMATION);
        if(failWorking) return FALSE;
        const auto ok=::K32QueryWorkingSetEx(h,p,n);
        if(ok && (invalidWorking || invalidTail)) {
            auto* rows=static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(p);
            for(DWORD i=0;i<n/sizeof(*rows);++i) if(invalidWorking || i==1) {
                rows[i].VirtualAttributes.Valid=0;
                rows[i].VirtualAttributes.Win32Protection=PAGE_EXECUTE_READWRITE;
            }
        }
        return ok;
    }
}
#define VirtualQuery Probe::Query
#define K32QueryWorkingSetEx Probe::Working
#include "../src/public_creative_compat.h"
#undef K32QueryWorkingSetEx
#undef VirtualQuery
int main() {
    unsigned cases=0,failed=0;
    auto check=[&](bool b,const char* s) {++cases;if(!b)++failed;std::cout<<(b?"PASS ":"FAIL ")<<s<<'\n';};
    auto* data=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!data)return 2; std::memset(data,0x5a,8192);
    std::array<unsigned char,4096> output{};
    const auto address=reinterpret_cast<std::uintptr_t>(data);
    auto protect=[&](unsigned offset,DWORD protection) {DWORD old=0; if(!VirtualProtect(data+offset,4096,protection,&old))std::exit(2);};
    Probe::queries=Probe::workingQueries=0;
    for(int i=0;i<32;++i)check(PublicCreativeCompat::Copy(address,output.data(),16),"resident RW copy");
    check(Probe::queries==0,"resident copies avoid full-region scans");
    check(Probe::workingQueries==32,"every resident copy uses a fresh page query");
    check(output[0]==0x5a,"exact source bytes copied");
    check(!PublicCreativeCompat::Copy(address,output.data(),16,true),"NX pages rejected for code");
    protect(0,PAGE_READONLY);
    check(PublicCreativeCompat::Copy(address,output.data(),16),"read-only data accepted");
    check(!PublicCreativeCompat::Copy(address,output.data(),16,true),"read-only NX code rejected");
    protect(0,PAGE_READWRITE|PAGE_GUARD);
    check(!PublicCreativeCompat::Copy(address,output.data(),16),"guard page rejected before access");
    MEMORY_BASIC_INFORMATION mbi{};::VirtualQuery(data,&mbi,sizeof(mbi));
    check((mbi.Protect&PAGE_GUARD)!=0,"guard flag remains armed");
    Probe::failWorking=true;
    check(!PublicCreativeCompat::Copy(address,output.data(),16),"failed page query still rejects guard");
    Probe::failWorking=false;Probe::invalidWorking=true;
    check(!PublicCreativeCompat::Copy(address,output.data(),16),"invalid admissible-looking metadata still rejects guard");
    Probe::invalidWorking=false;::VirtualQuery(data,&mbi,sizeof(mbi));
    check((mbi.Protect&PAGE_GUARD)!=0,"fallback guard remains armed");
    protect(0,PAGE_NOACCESS);
    check(!PublicCreativeCompat::Copy(address,output.data(),16),"no-access rejected");
    check(!PublicCreativeCompat::Copy(address+4090,output.data(),16),"cross-page inaccessible first page rejected");
    protect(0,PAGE_READWRITE); data[0]=0x5a;
    check(PublicCreativeCompat::Copy(address+4090,output.data(),16),"cross-page readable copy");
    check(Probe::lastRows==2,"cross-page query includes both pages");
    Probe::invalidTail=true; Probe::queries=0;
    check(PublicCreativeCompat::Copy(address+4090,output.data(),16) && Probe::queries>0 &&
        Probe::firstFallback==address+4090,"partial metadata falls back from original span start");
    Probe::invalidTail=false;
    protect(4096,PAGE_NOACCESS);
    check(!PublicCreativeCompat::Copy(address+4090,output.data(),16),"cross-page inaccessible tail rejected");
    protect(4096,PAGE_READWRITE|PAGE_GUARD);
    check(!PublicCreativeCompat::Copy(address+4090,output.data(),16),"cross-page guard tail rejected");
    protect(4096,PAGE_READWRITE); data[4096]=0x5a;
    Probe::failWorking=true; Probe::queries=0;
    check(PublicCreativeCompat::Copy(address,output.data(),16)&&Probe::queries>0,"query failure uses fresh region fallback");
    Probe::failWorking=false;Probe::invalidWorking=true;Probe::queries=0;
    check(PublicCreativeCompat::Copy(address,output.data(),16)&&Probe::queries>0,"nonresident metadata uses region fallback");
    Probe::invalidWorking=false;
    check(PublicCreativeCompat::Copy(address+1,output.data(),4096),"maximum copy across two pages");
    check(!PublicCreativeCompat::Copy(address,output.data(),4097),"oversized copy rejected");
    check(!PublicCreativeCompat::Copy(address,output.data(),0),"empty copy rejected");
    check(!PublicCreativeCompat::Copy(address,nullptr,16),"null output rejected");
    check(!PublicCreativeCompat::Copy(0,output.data(),16),"null source rejected");
    check(!PublicCreativeCompat::Copy(UINTPTR_MAX-2,output.data(),16),"overflow source rejected");
    // Executable page tests contain no instructions and are never called.
    protect(0,PAGE_EXECUTE_READ);
    check(PublicCreativeCompat::Copy(address,output.data(),16,true),"readable executable code accepted");
    protect(0,PAGE_EXECUTE);
    check(!PublicCreativeCompat::Copy(address,output.data(),16,true),"execute-only remains rejected");
    protect(0,PAGE_READWRITE);
    if(!VirtualFree(data,4096,MEM_DECOMMIT))return 2;
    check(!PublicCreativeCompat::Copy(address,output.data(),16),"decommitted source rejected");
    VirtualFree(data,0,MEM_RELEASE);
    check(!PublicCreativeCompat::Copy(address,output.data(),16),"freed source rejected");
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_EXECUTE_READWRITE,0,4096,nullptr);
    if(!mapping)return 2;
    for(DWORD access : {DWORD(FILE_MAP_COPY),DWORD(FILE_MAP_COPY|FILE_MAP_EXECUTE)}) {
        auto* view=static_cast<unsigned char*>(MapViewOfFile(mapping,access,0,0,4096));if(!view)return 2;
        // Read faults the page in without converting its write-copy protection.
        const volatile unsigned char observed=*view; (void)observed;
        const auto mapped=reinterpret_cast<std::uintptr_t>(view);
        check(PublicCreativeCompat::Copy(mapped,output.data(),16),"mapped write-copy is readable");
        check(PublicCreativeCompat::Copy(mapped,output.data(),16,true)==bool(access&FILE_MAP_EXECUTE),
            "mapped write-copy respects executable protection");
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping);
    std::cout<<"cases="<<cases<<" failed="<<failed<<'\n';return failed?1:0;
}
