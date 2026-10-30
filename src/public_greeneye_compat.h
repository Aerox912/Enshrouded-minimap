// Included inside PublicCreativeCompat. One owned snapshot, at most two exact
// providers. Never execute a provider or retain a positive foreign chain.
struct KnownProviderSnapshot
{
    std::array<unsigned char,2048> bytes{};
    std::size_t size=0;
    unsigned count=0;
    IdentityStamp stamp{};
    bool Add(const void* source,std::size_t length)
    {
        if(!source || length>bytes.size()-size)return false;
        std::memcpy(bytes.data()+size,source,length);size+=length;return true;
    }
    template<class T> bool Add(const T& value){return Add(&value,sizeof(value));}
};
inline bool SameKnownProviders(const KnownProviderSnapshot& a,const KnownProviderSnapshot& b)
{
    return a.count==b.count && a.size==b.size && a.stamp.epoch==b.stamp.epoch &&
        a.stamp.invalidation==b.stamp.invalidation && IdentityStampValid(a.stamp) &&
        !std::memcmp(a.bytes.data(),b.bytes.data(),a.size);
}
inline bool ReadKnownProviders(const unsigned char* prefix,std::uintptr_t renderer,KnownProviderSnapshot& value)
{
    value={};value.stamp=CurrentIdentityStamp();
    if(prefix[0]!=0x48 || prefix[1]!=0xb8 || prefix[10]!=0xff || prefix[11]!=0xe0)return false;
    for(std::size_t i=12;i<18;++i)if(prefix[i]!=0x90)return false;
    struct Node
    {
        HMODULE module=nullptr;
        std::uintptr_t base=0,relayAddress=0,callback=0,continuation=0;
        const Profile* profile=nullptr;FileCache* cache=nullptr;unsigned provider=0;
        Digest identity{};
        TrustedImageBytes expected{};
        bool deferred=false;
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
        std::array<unsigned char,0x5e> body{};
        std::array<unsigned char,32> relay{};
    };
    std::array<Node,2> nodes{};
    struct References
    {
        std::array<Node,2>& nodes;
        ~References(){for(auto& node:nodes)if(node.module)FreeLibrary(node.module);}
    } references{nodes};
    std::uintptr_t relayAddress=Word<std::uintptr_t>(prefix,2);
    std::array<unsigned char,32> relay{},terminal{};
    if(!Copy(relayAddress,relay.data(),relay.size(),true))return false;
    unsigned seen=0;
    bool complete=false;
    for(unsigned index=0;index<nodes.size();++index)
    {
        auto& node=nodes[index];
        constexpr unsigned char jump[6]={0xff,0x25,10,0,0,0};
        if(!Range(relayAddress,relay.size()) || std::memcmp(relay.data(),jump,sizeof(jump)))return false;
        for(std::size_t i=6;i<16;++i)if(relay[i])return false;
        for(unsigned i=0;i<index;++i)if(nodes[i].relayAddress==relayAddress)return false;
        node.relayAddress=relayAddress;node.relay=relay;
        node.callback=Word<std::uintptr_t>(relay.data(),16);
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(node.callback),&node.module))
        {ForgetIdentity();return false;}
        node.base=reinterpret_cast<std::uintptr_t>(node.module);
        if(Range(node.base,Find112.imageSize) && node.callback==node.base+Find112.callback)
        {node.profile=&Find112;node.cache=&findFileCache;node.provider=1;}
        else if(Range(node.base,Teleport116.imageSize) && node.callback==node.base+Teleport116.callback)
        {node.profile=&Teleport116;node.cache=&teleportFileCache;node.provider=2;}
        else return false;
        if(seen&node.provider)return false;
        seen|=node.provider;
        for(unsigned i=0;i<index;++i)if(nodes[i].base==node.base)return false;
        if(!Copy(node.base,&node.dos,sizeof(node.dos)) || node.dos.e_magic!=IMAGE_DOS_SIGNATURE || node.dos.e_lfanew<=0 ||
            static_cast<std::size_t>(node.dos.e_lfanew)>4096-sizeof(node.nt) ||
            !Copy(node.base+node.dos.e_lfanew,&node.nt,sizeof(node.nt)) || node.nt.Signature!=IMAGE_NT_SIGNATURE ||
            node.nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || node.nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            node.nt.OptionalHeader.SizeOfImage!=node.profile->imageSize)
        {ForgetIdentity();return false;}
        const Profile* actual=nullptr;
        if(!FileIdentity(node.module,node.identity,actual,*node.cache,node.profile,&node.expected,true,&node.deferred) &&
            !node.deferred)return false;
        if(!node.deferred && (actual!=node.profile ||
            std::memcmp(&node.dos,&node.expected.dos,sizeof(node.dos)) ||
            std::memcmp(&node.nt,&node.expected.nt,sizeof(node.nt))))
        {ForgetIdentity();return false;}
        if(!Copy(node.callback,node.body.data(),node.profile->bodySize,true)){ForgetIdentity();return false;}
        if(!Copy(node.base+node.profile->slot,&node.continuation,sizeof(node.continuation)))return false;
        if(!node.deferred && std::memcmp(node.body.data(),node.expected.body.data(),node.profile->bodySize))
        {ForgetIdentity();return false;}
        ++value.count;
        // A continuation is either the complete original trampoline or a prior
        // relay. Prefetch once, then classify; never recursively double-prove it.
        std::array<unsigned char,32> next{};
        if(!Copy(node.continuation,next.data(),next.size(),true))return false;
        if(!std::memcmp(next.data(),Original,sizeof(Original)))
        {
            constexpr unsigned char jumpBack[6]={0xff,0x25,0,0,0,0};
            std::uintptr_t resume=0;
            if(!std::memcmp(next.data()+18,jumpBack,sizeof(jumpBack)))resume=Word<std::uintptr_t>(next.data(),24);
            else if(node.profile==&Find112 && next[18]==0xe9)
            {if(!Relative(node.continuation+18,Word<std::int32_t>(next.data(),19),resume))return false;}
            else return false;
            if(resume!=renderer+18)return false;
            terminal=next;complete=true;break;
        }
        if(index+1==nodes.size() || node.continuation==renderer || node.continuation==relayAddress)return false;
        relayAddress=node.continuation;relay=next;
    }
    // Reject unknown/duplicate/cyclic topology before spending a provider's
    // single admission attempt. Cold authentication owns no positive chain.
    if(!complete || !value.Add(prefix,18))return false;
    for(unsigned index=0;index<value.count;++index)
    {
        auto& node=nodes[index];
        const Profile* actual=nullptr;
        if(node.deferred && !FileIdentity(node.module,node.identity,actual,*node.cache,node.profile,&node.expected))return false;
        if(std::memcmp(&node.dos,&node.expected.dos,sizeof(node.dos)) ||
            std::memcmp(&node.nt,&node.expected.nt,sizeof(node.nt)) ||
            std::memcmp(node.body.data(),node.expected.body.data(),node.profile->bodySize))
        {ForgetIdentity();return false;}
        if(!value.Add(node.provider) || !value.Add(node.base) || !value.Add(node.relayAddress) || !value.Add(node.callback) ||
            !value.Add(node.identity) || !value.Add(node.dos) || !value.Add(node.nt) ||
            !value.Add(node.body.data(),node.profile->bodySize) || !value.Add(node.relay) || !value.Add(node.continuation))return false;
    }
    return value.Add(terminal) && IdentityStampValid(value.stamp);
}
