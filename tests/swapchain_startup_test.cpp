#include "../src/dllmain.cpp"
#include <iostream>

namespace
{
    struct FakeObservations
    {
        std::uint32_t createCalls = 0;
        std::uint32_t allocationCalls = 0;
        std::uint32_t submitCalls = 0;
        std::uint32_t nextHandle = 0x10000;
        std::uint32_t renderPassFormat = 0;
        std::uint32_t imageViewFormat = 0;
        std::uint32_t framebufferWidth = 0;
        std::uint32_t framebufferHeight = 0;
    } fake;

    std::array<std::uintptr_t, 32> fakeDeviceTable{};
    int failures = 0;

    void Check(bool ok, const char* description)
    {
        if (!ok)
        {
            ++failures;
            std::cout << "FAIL: " << description << '\n';
        }
        else
        {
            std::cout << "PASS: " << description << '\n';
        }
    }

    void* NextHandle()
    {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(fake.nextHandle++));
    }

    void __fastcall FakeUnusedProc() {}

    std::int32_t __fastcall FakeGetSwapchainImages(void*, void*, std::uint32_t* count, void* images)
    {
        if (count == nullptr)
            return -1;
        if (images == nullptr)
        {
            *count = 2;
            return VK_SUCCESS;
        }
        auto** output = reinterpret_cast<void**>(images);
        output[0] = reinterpret_cast<void*>(0x9100);
        output[1] = reinterpret_cast<void*>(0x9101);
        *count = 2;
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeCreateCommandPool(void*, const VkCommandPoolCreateInfo*, const void*, void** output)
    {
        ++fake.createCalls;
        *output = NextHandle();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeCreateRenderPass(void*, const VkRenderPassCreateInfo* info, const void*, void** output)
    {
        ++fake.createCalls;
        fake.renderPassFormat = info->pAttachments[0].format;
        *output = NextHandle();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeCreateImageView(void*, const VkImageViewCreateInfo* info, const void*, void** output)
    {
        ++fake.createCalls;
        fake.imageViewFormat = info->format;
        *output = NextHandle();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeCreateFramebuffer(void*, const VkFramebufferCreateInfo* info, const void*, void** output)
    {
        ++fake.createCalls;
        fake.framebufferWidth = info->width;
        fake.framebufferHeight = info->height;
        *output = NextHandle();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeAllocateCommandBuffers(void*, const VkCommandBufferAllocateInfo* info, void** output)
    {
        ++fake.allocationCalls;
        for (std::uint32_t i = 0; i < info->commandBufferCount; ++i)
            output[i] = NextHandle();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeCreateSemaphore(void*, const VkSemaphoreCreateInfo*, const void*, void** output)
    {
        ++fake.createCalls;
        *output = NextHandle();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeCreateFence(void*, const VkFenceCreateInfo*, const void*, void** output)
    {
        ++fake.createCalls;
        *output = NextHandle();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall FakeQueueSubmit(void*, std::uint32_t, const VkSubmitInfo*, void*)
    {
        ++fake.submitCalls;
        return VK_SUCCESS;
    }

    void* __fastcall FakeGetDeviceProcAddr(void*, const char* name)
    {
        if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0) return reinterpret_cast<void*>(&FakeGetSwapchainImages);
        if (std::strcmp(name, "vkCreateCommandPool") == 0) return reinterpret_cast<void*>(&FakeCreateCommandPool);
        if (std::strcmp(name, "vkCreateRenderPass") == 0) return reinterpret_cast<void*>(&FakeCreateRenderPass);
        if (std::strcmp(name, "vkCreateImageView") == 0) return reinterpret_cast<void*>(&FakeCreateImageView);
        if (std::strcmp(name, "vkCreateFramebuffer") == 0) return reinterpret_cast<void*>(&FakeCreateFramebuffer);
        if (std::strcmp(name, "vkAllocateCommandBuffers") == 0) return reinterpret_cast<void*>(&FakeAllocateCommandBuffers);
        if (std::strcmp(name, "vkCreateSemaphore") == 0) return reinterpret_cast<void*>(&FakeCreateSemaphore);
        if (std::strcmp(name, "vkCreateFence") == 0) return reinterpret_cast<void*>(&FakeCreateFence);
        if (std::strcmp(name, "vkQueueSubmit") == 0) return reinterpret_cast<void*>(&FakeQueueSubmit);
        return reinterpret_cast<void*>(&FakeUnusedProc);
    }

    void SetupFakeEnvironment()
    {
        fake = {};
        g_renderer = VulkanMinimapRenderer{};
        g_swapchains.clear();
        fakeDeviceTable.fill(0);
        fakeDeviceTable[VULKAN_TABLE_GET_DEVICE_PROC_ADDR_OFFSET / sizeof(std::uintptr_t)] =
            reinterpret_cast<std::uintptr_t>(&FakeGetDeviceProcAddr);
        g_vulkanDeviceTable.store(reinterpret_cast<std::uintptr_t>(fakeDeviceTable.data()));
        g_originalGetSwapchainImages.store(0);
        g_lastVulkanQueueFamilyIndex.store(0);
        g_modContext = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_minimapFrameMutex);
            g_minimapFrame.attempted = true;
            g_minimapFrame.loaded = false;
            g_minimapFrame.rgba.clear();
            g_minimapFrame.width = 0;
            g_minimapFrame.height = 0;
        }
    }

    SwapchainRuntimeInfo MakeImagesOnlySnapshot(std::uintptr_t swapchain)
    {
        void* images[2] = { reinterpret_cast<void*>(0x9100), reinterpret_cast<void*>(0x9101) };
        std::uint32_t count = 2;
        RememberSwapchainImages(reinterpret_cast<void*>(0x2000), reinterpret_cast<void*>(swapchain),
            &count, images, VK_SUCCESS);
        SwapchainRuntimeInfo snapshot{};
        if (!TryGetSwapchainSnapshot(swapchain, snapshot))
            return {};
        return snapshot;
    }
}

namespace
{
    std::array<uintptr_t, 0x600/8> startupTable{}, otherTable{};
    std::array<void*,4> expectedArguments{};
    unsigned initCalls = 0, presentCalls = 0;
    std::uint8_t initResult = 0xa5;
    int initMutation = 0;
    bool argumentsExact = false, observedBeforeOriginal = false;
    void* forwardedQueue = nullptr;
    const void* forwardedPresent = nullptr;

    std::int32_t __fastcall NativeCreate(void*, const VkSwapchainCreateInfoKHR*, const void*, void**) { return -7; }
    std::int32_t __fastcall NativeImages(void*, void*, std::uint32_t*, void*) { return -8; }
    std::int32_t __fastcall NativePresent(void* queue, const void* info)
    {
        ++presentCalls; forwardedQueue=queue; forwardedPresent=info; return -1000001004;
    }
    void* __fastcall StartupResolver(void*, const char* name)
    {
        if (!std::strcmp(name,"vkCreateSwapchainKHR")) return reinterpret_cast<void*>(&NativeCreate);
        if (!std::strcmp(name,"vkGetSwapchainImagesKHR")) return reinterpret_cast<void*>(&NativeImages);
        if (!std::strcmp(name,"vkQueuePresentKHR")) return reinterpret_cast<void*>(&NativePresent);
        return nullptr;
    }
    std::uint8_t __fastcall NativeInitializer(void* table, void* owner, void* device, void* record)
    {
        ++initCalls;
        argumentsExact = expectedArguments == std::array<void*,4>{table,owner,device,record};
        observedBeforeOriginal = g_patchedVulkanDeviceTable.load()!=0;
        auto* t=static_cast<uintptr_t*>(table);
        t[0]=reinterpret_cast<uintptr_t>(owner);
        t[1]=reinterpret_cast<uintptr_t>(device);
        t[0x78/8]=reinterpret_cast<uintptr_t>(&StartupResolver);
        t[0x580/8]=reinterpret_cast<uintptr_t>(&NativeCreate);
        t[0x590/8]=reinterpret_cast<uintptr_t>(&NativeImages);
        t[0x5a0/8]=reinterpret_cast<uintptr_t>(&NativePresent);
        if (initMutation==1) ++t[1];
        if (initMutation==2) ++t[0];
        if (initMutation==3) t[0x590/8]=reinterpret_cast<uintptr_t>(&FakeUnusedProc);
        if (initMutation==4) *reinterpret_cast<std::uint8_t*>(g_vulkanStartup.callsite)=0x90;
        if (initMutation==5) DisableVulkanStartupCapture();
        if (initMutation==6) t[0x78/8]=0;
        if (initMutation==7) t[0x580/8]=reinterpret_cast<uintptr_t>(&FakeUnusedProc);
        if (initMutation==8) t[0x5a0/8]=reinterpret_cast<uintptr_t>(&FakeUnusedProc);
        return initResult;
    }
    Mem::Detour* PrepareInertCallsite(VulkanStartupObserver& observer)
    {
        auto* page=static_cast<std::uint8_t*>(Mem::AllocateMemoryNearAddress(reinterpret_cast<void*>(&NativeInitializer),4096));
        Check(page!=nullptr,"allocate controlled inert native callsite");
        std::memset(page,0x90,4096);
        // Controlled Win64 function: sub rsp,28h; CALL relay; add rsp,28h; RET.
        // CALL at +11 retains the required byte-3 aligned-word placement.
        const std::uint8_t prologue[]={0x48,0x83,0xec,0x28};
        const std::uint8_t epilogue[]={0x48,0x83,0xc4,0x28,0xc3};
        std::memcpy(page+7,prologue,4); std::memcpy(page+16,epilogue,5);
        observer.callsite=reinterpret_cast<uintptr_t>(page+11);
        std::uint8_t call[5]{};
        Check(MakeDirectCallOpcode(observer.callsite,reinterpret_cast<uintptr_t>(&NativeInitializer),call),"fixture original within CALL range");
        std::memcpy(page+11,call,5);
        std::memcpy(&observer.preimage,page+8,8);
        std::uint8_t relay[14]={0xff,0x25,0,0,0,0};
        const auto handler=reinterpret_cast<uintptr_t>(&CaptureVulkanStartup);
        std::memcpy(relay+6,&handler,8);
        auto* hook=new Mem::Detour(observer.callsite,relay,14,false);
        const std::uint8_t opcode=0xe8;
        hook->patch->updateOpcode(0,const_cast<std::uint8_t*>(&opcode),1);
        observer.original.store(&NativeInitializer);
        observer.hook=hook;
        return hook;
    }
    void SetupStartup()
    {
        startupTable.fill(0); otherTable.fill(0);
        g_client10=true; g_clientProfile=MinimapClient10::Profile::Build26082781;
        g_patchedVulkanDeviceTable=0; g_patchedVulkanDevice=0; g_vulkanDeviceTable=0;
        g_originalCreateSwapchain=0; g_originalGetSwapchainImages=0; g_originalQueuePresent=0;
        g_lastVulkanDevice=0; initCalls=0; initMutation=0; initResult=0xa5;
        argumentsExact=false; observedBeforeOriginal=false;
        g_vulkanStartup.hook=nullptr; g_vulkanStartup.finalized=false;
        g_vulkanStartup.capture=false;
        PrepareInertCallsite(g_vulkanStartup);
        Check(PublishVulkanStartupCallsite(g_vulkanStartup,[]{}),"checked atomic publication succeeds with exact preimage");
        g_vulkanStartup.capture=true;
        expectedArguments={startupTable.data(),reinterpret_cast<void*>(0x4455),reinterpret_cast<void*>(0x2000),reinterpret_cast<void*>(0x8899)};
    }
    std::uint8_t InvokeStartup()
    {
        // Execute only controlled inert bytes and our own compiled callbacks.
        auto entry=reinterpret_cast<VulkanStartupInitFn>(g_vulkanStartup.callsite-4);
        return entry(expectedArguments[0],expectedArguments[1],expectedArguments[2],expectedArguments[3]);
    }
    bool FinalizeFailure(uintptr_t address,bool replaced,DWORD protection)
    {
        FinalizeMenuCallsitePublication(address,replaced,protection); return false;
    }

    void CheckRawStartupGuards()
    {
        auto* fixture=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x1000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        Check(fixture!=nullptr,"allocate non-executable bounded static guard fixture");
        if(!fixture) return;
        const std::uint8_t caller[]={0x0f,0xb6,0x83,0x00,0x02,0x00,0x00,0x4c,0x8d,0x4d,0x10,0x4c,0x8b,0x87,0x88,0x07,0x00,0x00,0x48,0x8b,0x97,0x80,0x07,0x00,0x00,0x48,0x8b,0x8f,0x70,0x07,0x00,0x00,0x48,0x89,0x45,0x18,0x48,0x89,0x5d,0x10,0xe8,0x68,0x16,0x02,0x00,0x84,0xc0,0x74,0x76,0x48,0x8b,0xb5,0x78,0x0a,0x00,0x00,0x4c,0x8d,0x45,0x10,0x48,0x8b,0xce,0x0f,0xb6,0xd0,0xe8,0xae,0xa1,0xfe,0xff}; const std::uint8_t initializer[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x58,0x48,0x8b,0x41,0x78,0x48,0x8d,0x1d,0xc1,0xe5,0xdb,0x00,0x48,0x89,0x11,0x48,0x8b,0xf9,0x4c,0x89,0x41,0x08,0x33,0xf6,0x48,0x8b,0xd3,0x89,0x75,0x54,0x49,0x8b,0xc8,0x40,0x88,0x75,0x50,0x4d,0x8b,0xf9,0x4d,0x8b,0xf0,0xff,0xd0,0x48,0x89,0x45,0x60,0x44,0x8b,0xee,0x40,0x88,0x75,0x48,0x44,0x8b,0xe6};
        std::memcpy(fixture+0xFBF54B,caller,sizeof(caller));
        std::memcpy(fixture+0xFE0BE0,initializer,sizeof(initializer));
        const auto oldBase=g_exeBase; const auto oldSize=g_exeImageSize;
        g_exeBase=reinterpret_cast<uintptr_t>(fixture); g_exeImageSize=0x1000000;
        g_vulkanStartup.hook=nullptr; g_clientProfile=MinimapClient10::Profile::Build26082781;
        Check(MatchVulkanStartupCallsite(),"current profile accepts exact pinned argument/call/AL-test and callee guard bytes");
        for(auto offset : {0xFBF552,0xFBF573,0xFBF574,0xFBF578,0xFE0BE0,0xFE0C05})
        {
            fixture[offset]^=1;
            Check(!MatchVulkanStartupCallsite(),"changed argument/call/return/initializer bytes reject startup admission");
            fixture[offset]^=1;
        }
        g_clientProfile=MinimapClient10::Profile::Build25988036;
        Check(!MatchVulkanStartupCallsite() && MinimapClient10::VulkanStartupCallsiteRva(g_clientProfile)==0 &&
            MinimapClient10::VulkanStartupInitializerRva(g_clientProfile)==0,"older build has no inferred startup callsite mapping");
        g_exeBase=oldBase; g_exeImageSize=oldSize; VirtualFree(fixture,0,MEM_RELEASE);
    }
    void CheckStartup()
    {
        SetupStartup(); initResult=0;
        Check(InvokeStartup()==0 && initCalls==1 && argumentsExact && !observedBeforeOriginal,
            "zero native status is returned byte-for-byte after one exact four-argument original call");
        Check(startupTable[0x580/8]==reinterpret_cast<uintptr_t>(&HookCreateSwapchain) &&
            startupTable[0x590/8]==reinterpret_cast<uintptr_t>(&HookGetSwapchainImages) &&
            startupTable[0x5a0/8]==reinterpret_cast<uintptr_t>(&HookQueuePresent) &&
            g_vulkanDeviceTable.load()==reinterpret_cast<uintptr_t>(startupTable.data()) &&
            g_lastVulkanDevice.load()==0x2000 && fake.createCalls==0,
            "successful initializer installs existing table hooks and captures exact device without renderer allocation");
        for (auto result : {std::uint8_t(0x1f),std::uint8_t(1),std::uint8_t(0x80),std::uint8_t(0xff)})
        {
            SetupStartup(); initResult=result;
            Check(InvokeStartup()==result && initCalls==1 && argumentsExact &&
                g_patchedVulkanDeviceTable.load()==0 &&
                startupTable[0x580/8]==reinterpret_cast<uintptr_t>(&NativeCreate) &&
                startupTable[0x590/8]==reinterpret_cast<uintptr_t>(&NativeImages) &&
                startupTable[0x5a0/8]==reinterpret_cast<uintptr_t>(&NativePresent),
                "every nonzero initializer status passes through unchanged and rejects table installation");
        }
        SetupStartup(); initResult=0; expectedArguments[1]=nullptr; expectedArguments[3]=nullptr;
        Check(InvokeStartup()==0 && initCalls==1 && argumentsExact &&
            startupTable[0x580/8]==reinterpret_cast<uintptr_t>(&HookCreateSwapchain),
            "zero status preserves opaque null native arguments and installs hooks");
        SetupStartup(); g_vulkanStartup.original.store(nullptr);
        Check(InvokeStartup()==0 && initCalls==0 && g_patchedVulkanDeviceTable.load()==0 &&
            startupTable[0x580/8]==0 && startupTable[0x590/8]==0 && startupTable[0x5a0/8]==0,
            "absent native original fails closed without a fabricated initializer call or table install");
        for(int mutation=1;mutation<=8;++mutation)
        {
            SetupStartup(); initMutation=mutation; initResult=0;
            const auto result=InvokeStartup();
            Check(result==initResult && initCalls==1 && argumentsExact && g_patchedVulkanDeviceTable.load()==0 &&
                startupTable[0x580/8]==reinterpret_cast<uintptr_t>(mutation==7 ? reinterpret_cast<void*>(&FakeUnusedProc) : reinterpret_cast<void*>(&NativeCreate)) &&
                startupTable[0x590/8]!=reinterpret_cast<uintptr_t>(&HookGetSwapchainImages) &&
                startupTable[0x5a0/8]!=reinterpret_cast<uintptr_t>(&HookQueuePresent),
                "failed init, identity, slot, callsite ownership or lifecycle epoch rejection installs no table hooks");
        }
        SetupStartup();
        initResult=0;
        g_patchedVulkanDeviceTable=reinterpret_cast<uintptr_t>(otherTable.data()); g_patchedVulkanDevice=0x2000;
        Check(InvokeStartup()==0 && startupTable[0x580/8]==reinterpret_cast<uintptr_t>(&NativeCreate),
            "another table cannot steal the lifetime forwarding owner");
        SetupStartup();
        initResult=0;
        g_patchedVulkanDeviceTable=reinterpret_cast<uintptr_t>(startupTable.data()); g_patchedVulkanDevice=0x3000;
        Check(InvokeStartup()==0 && startupTable[0x580/8]==reinterpret_cast<uintptr_t>(&NativeCreate),
            "same table on changed device cannot reuse callback originals");
        SetupStartup(); initResult=0; auto* retained=g_vulkanStartup.hook; auto original=g_vulkanStartup.original.load();
        DisableVulkanStartupCapture();
        Check(g_vulkanStartup.hook==retained && original==g_vulkanStartup.original.load() &&
            retained->active && IsVulkanStartupOwned(g_vulkanStartup) && InvokeStartup()==0 && initCalls==1 &&
            g_patchedVulkanDeviceTable.load()==0,
            "deactivation retains reachable relay/original and forwards exactly once without capture");
        VulkanStartupObserver conflict;
        auto* hook=PrepareInertCallsite(conflict);
        std::uint64_t changed=conflict.preimage ^ 1;
        Check(!PublishVulkanStartupCallsite(conflict,[&]{std::memcpy(reinterpret_cast<void*>(conflict.callsite-3),&changed,8);}) &&
            !hook->active && *reinterpret_cast<std::uint64_t*>(conflict.callsite-3)==changed,
            "atomic conflict preserves foreign changed preimage including neighboring bytes");
        VulkanStartupObserver finalize;
        hook=PrepareInertCallsite(finalize);
        Check(!PublishVulkanStartupCallsite(finalize,[]{},FinalizeFailure) && hook->active &&
            !finalize.finalized && IsVulkanStartupOwned(finalize),
            "failed publication finalization retains reachable relay and trusted forwarding state");
        VulkanStartupObserver missing;
        hook=PrepareInertCallsite(missing); missing.original=nullptr;
        Check(!PublishVulkanStartupCallsite(missing,[]{}) && !hook->active,
            "publication rejects absent original before callsite mutation");
        missing.original=&NativeInitializer;
        hook->patch->data->opcode[1]^=1;
        Check(!PublishVulkanStartupCallsite(missing,[]{}) && !hook->active,
            "publication rejects a CALL patch targeting an unknown relay");
        hook->patch->data->opcode[1]^=1;
        hook->shellcode->data->opcode[0]=0x90;
        Check(!PublishVulkanStartupCallsite(missing,[]{}) && !hook->active,
            "publication rejects foreign relay bytes before callsite mutation");
        hook->shellcode->data->opcode[0]=0xff; const auto allocation=hook->shellcode->data->address;
        hook->shellcode->data->address=0;
        Check(!PublishVulkanStartupCallsite(missing,[]{}) && !hook->active,
            "missing relay allocation never publishes a CALL");
        hook->shellcode->data->address=allocation;
        Check(!PublishVulkanStartupCallsite(finalize,[]{}),"reachable failed-finalization hook cannot be republished");
        g_vulkanStartup.capture=false; g_client10=false;
        g_patchedVulkanDevice=0; g_patchedVulkanDeviceTable=0;
    }
    void CheckMetadata()
    {
        SetupFakeEnvironment();
        SwapchainRuntimeInfo handleOnly{}; handleOnly.handle=0xb000; handleOnly.device=0x2000;
        Check(!BuildVulkanMinimapRendererLocked(handleOnly) && fake.createCalls==0,"handle/device-only record stays pending");
        VkSwapchainCreateInfoKHR create{}; create.imageFormat=44; create.imageExtent={777,555};
        const auto device=reinterpret_cast<void*>(0x2000), handle=reinterpret_cast<void*>(0xb000);
        RememberSwapchainCreate(device,&create,handle,-1);
        SwapchainRuntimeInfo snapshot{};
        Check(!TryGetSwapchainSnapshot(0xb000,snapshot),"failed creation cannot establish metadata provenance");
        RememberSwapchainCreate(device,&create,handle,VK_SUCCESS);
        TryGetSwapchainSnapshot(0xb000,snapshot);
        Check(snapshot.colorSpace==0 && HasObservedSwapchainCreate(snapshot) && BuildVulkanMinimapRendererLocked(snapshot) &&
            g_renderer.width==777 && g_renderer.height==555 && g_renderer.format==44 && g_renderer.colorSpace==0,
            "valid colorSpace zero is captured exactly and builds through production path");
        const auto generation=snapshot.generation;
        snapshot.generation++; auto before=fake.createCalls;
        Check(!BuildVulkanMinimapRendererLocked(snapshot) && fake.createCalls==before,"generation mismatch cannot reuse cached renderer or create resources");
        snapshot.generation=generation; snapshot.handle++;
        Check(!BuildVulkanMinimapRendererLocked(snapshot) && fake.createCalls==before,"handle mismatch cannot inherit create provenance");
        snapshot.handle=0xb000; snapshot.device++;
        Check(!BuildVulkanMinimapRendererLocked(snapshot) && fake.createCalls==before,"device mismatch cannot inherit create provenance");
        RememberSwapchainCreate(device,&create,handle,VK_SUCCESS); TryGetSwapchainSnapshot(0xb000,snapshot);
        Check(snapshot.generation>generation && !RendererMatchesSwapchain(g_renderer,snapshot,777,555,44),"same-handle recreation invalidates cached generation");
        void* images[]={reinterpret_cast<void*>(0x1000)}; std::uint32_t count=1;
        RememberSwapchainImages(reinterpret_cast<void*>(0x3000),handle,&count,images,VK_SUCCESS);
        TryGetSwapchainSnapshot(0xb000,snapshot);
        Check(snapshot.device==0x3000 && !HasObservedSwapchainCreate(snapshot) && snapshot.width==0 && snapshot.format==0 &&
            !BuildVulkanMinimapRendererLocked(snapshot) && fake.createCalls==before,
            "enumeration on changed device discards stale properties and cannot manufacture metadata");
        for(int missing=0;missing<3;++missing)
        {
            SetupFakeEnvironment();
            create.imageFormat=missing==0?0:44; create.imageExtent={missing==1?0u:777u,missing==2?0u:555u};
            RememberSwapchainCreate(device,&create,handle,VK_SUCCESS); TryGetSwapchainSnapshot(0xb000,snapshot);
            Check(!BuildVulkanMinimapRendererLocked(snapshot) && fake.createCalls==0 && fake.allocationCalls==0,
                "partial successful create metadata cannot allocate renderer resources");
        }
        SetupFakeEnvironment(); g_runtimeCallbacksActive=false; g_originalQueuePresent=reinterpret_cast<uintptr_t>(&NativePresent);
        presentCalls=0; VkPresentInfoKHR info{}; info.swapchainCount=1;
        void* swap=handle; std::uint32_t index=0; info.pSwapchains=&swap; info.pImageIndices=&index;
        const auto queue=reinterpret_cast<void*>(0xabcdef);
        Check(HookQueuePresent(queue,&info)==-1000001004 && presentCalls==1 && forwardedQueue==queue && forwardedPresent==&info &&
            fake.createCalls==0 && fake.submitCalls==0,"unavailable overlay forwards native present input/result exactly once");
        g_runtimeCallbacksActive=true;
        VkPresentInfoKHR adjusted{}; const void* waits[1]{};
        Check(!TrySubmitVulkanMinimap(queue,info,0xb000,0,adjusted,waits,false) && fake.createCalls==0 && fake.submitCalls==0,
            "unknown record cannot submit or allocate with runtime callbacks enabled");
        // Keep normal config/function discovery outside this controlled present case.
        g_lastConfigPollTick=GetTickCount(); g_vulkanFunctionOffsetsLogged=true;
        presentCalls=0;
        Check(HookQueuePresent(queue,&info)==-1000001004 && presentCalls==1 && forwardedQueue==queue && forwardedPresent==&info &&
            fake.createCalls==0 && fake.allocationCalls==0 && fake.submitCalls==0,
            "active unknown-metadata present forwards original inputs/result once with zero renderer work");
        g_runtimeCallbacksActive=false;
    }
}

namespace
{
    // Guard bytes are static data in an RW fixture. Only the separate inert
    // native CALL/relay and our compiled callbacks are ever executed.
    struct StartupGuardFixture
    {
        std::uint8_t* bytes=nullptr;
        uintptr_t oldBase=g_exeBase;
        std::size_t oldSize=g_exeImageSize;
        StartupGuardFixture()
        {
            bytes=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x1000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            Check(bytes!=nullptr,"activation-window fixture has non-executable pinned code guards");
            if(!bytes) return;
            const std::uint8_t caller[]={0x0f,0xb6,0x83,0x00,0x02,0x00,0x00,0x4c,0x8d,0x4d,0x10,0x4c,0x8b,0x87,0x88,0x07,0x00,0x00,0x48,0x8b,0x97,0x80,0x07,0x00,0x00,0x48,0x8b,0x8f,0x70,0x07,0x00,0x00,0x48,0x89,0x45,0x18,0x48,0x89,0x5d,0x10,0xe8,0x68,0x16,0x02,0x00,0x84,0xc0,0x74,0x76,0x48,0x8b,0xb5,0x78,0x0a,0x00,0x00,0x4c,0x8d,0x45,0x10,0x48,0x8b,0xce,0x0f,0xb6,0xd0,0xe8,0xae,0xa1,0xfe,0xff}; const std::uint8_t initializer[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x58,0x48,0x8b,0x41,0x78,0x48,0x8d,0x1d,0xc1,0xe5,0xdb,0x00,0x48,0x89,0x11,0x48,0x8b,0xf9,0x4c,0x89,0x41,0x08,0x33,0xf6,0x48,0x8b,0xd3,0x89,0x75,0x54,0x49,0x8b,0xc8,0x40,0x88,0x75,0x50,0x4d,0x8b,0xf9,0x4d,0x8b,0xf0,0xff,0xd0,0x48,0x89,0x45,0x60,0x44,0x8b,0xee,0x40,0x88,0x75,0x48,0x44,0x8b,0xe6};
            std::memcpy(bytes+0xFBF54B,caller,sizeof(caller));
            std::memcpy(bytes+0xFE0BE0,initializer,sizeof(initializer));
            g_exeBase=reinterpret_cast<uintptr_t>(bytes); g_exeImageSize=0x1000000;
        }
        ~StartupGuardFixture()
        {
            g_exeBase=oldBase;g_exeImageSize=oldSize;
            if(bytes) VirtualFree(bytes,0,MEM_RELEASE);
        }
    };
    std::uint32_t windowCreateCalls=0,windowImageCalls=0;
    void* windowCreateHandle=nullptr;
    std::int32_t __fastcall WindowNativeCreate(void*, const VkSwapchainCreateInfoKHR*, const void*,void** output)
    {
        ++windowCreateCalls;if(output) *output=windowCreateHandle;return VK_SUCCESS;
    }
    std::int32_t __fastcall WindowNativeImages(void*,void*,std::uint32_t* count,void* images)
    {
        ++windowImageCalls;if(!count) return -1;
        if(!images){*count=1;return VK_SUCCESS;}
        if(*count<1) return -1;
        reinterpret_cast<void**>(images)[0]=reinterpret_cast<void*>(0x9200);*count=1;return VK_SUCCESS;
    }
    void SetWindowOriginals()
    {
        g_originalCreateSwapchain=reinterpret_cast<uintptr_t>(&WindowNativeCreate);
        g_originalGetSwapchainImages=reinterpret_cast<uintptr_t>(&WindowNativeImages);
        windowCreateCalls=0;windowImageCalls=0;
    }
    VkSwapchainCreateInfoKHR WindowCreateInfo()
    {
        VkSwapchainCreateInfoKHR create{};create.imageFormat=44;create.imageColorSpace=1000104001;
        create.imageExtent={777,555};return create;
    }
    void CheckActivationWindow()
    {
        SetupFakeEnvironment();SetupStartup();StartupGuardFixture guards;
        if(!guards.bytes) return;
        g_runtimeCallbacksActive=false;g_vulkanStartup.capture=false;
        // Exercise the real checked activation boundary. The module's later
        // runtime-active assignment has deliberately not happened here.
        Check(ActivateVulkanStartupCallsite(),"checked bootstrap activation succeeds after core prerequisites");
        Check(g_runtimeCallbacksActive.load(std::memory_order_acquire),
            "successful bootstrap publishes callback readiness before releasing its observer barrier");
        initResult=0;
        Check(InvokeStartup()==0 && initCalls==1 && argumentsExact &&
            startupTable[0x580/8]==reinterpret_cast<uintptr_t>(&HookCreateSwapchain) &&
            startupTable[0x590/8]==reinterpret_cast<uintptr_t>(&HookGetSwapchainImages) &&
            startupTable[0x5a0/8]==reinterpret_cast<uintptr_t>(&HookQueuePresent) &&
            g_patchedVulkanDeviceTable.load()==reinterpret_cast<uintptr_t>(startupTable.data()) &&
            g_lastVulkanDevice.load()==0x2000,
            "zero initializer status installs all production hooks before the first controlled create or images callback");
        SetWindowOriginals();
        auto create=WindowCreateInfo();void* handle=reinterpret_cast<void*>(0xb100);windowCreateHandle=handle;
        auto createHook=reinterpret_cast<CreateSwapchainFn>(startupTable[0x580/8]);
        const auto result=createHook(reinterpret_cast<void*>(0x2000),&create,nullptr,&handle);
        SwapchainRuntimeInfo snapshot{};
        const bool recorded=TryGetSwapchainSnapshot(reinterpret_cast<uintptr_t>(handle),snapshot);
        Check(result==VK_SUCCESS && windowCreateCalls==1 && recorded && HasObservedSwapchainCreate(snapshot) &&
            snapshot.format==44 && snapshot.colorSpace==1000104001 && snapshot.width==777 && snapshot.height==555,
            "first table create after zero-status initialization captures exact native metadata once");
        // Model the later module continuation. Enumeration must preserve actual
        // selected properties; it cannot be the source of those properties.
        g_runtimeCallbacksActive=true;
        auto imagesHook=reinterpret_cast<GetSwapchainImagesFn>(startupTable[0x590/8]);
        std::uint32_t count=0;
        const auto imageCountResult=imagesHook(reinterpret_cast<void*>(0x2000),handle,&count,nullptr);
        void* image=nullptr;
        const auto imageResult=imagesHook(reinterpret_cast<void*>(0x2000),handle,&count,&image);
        Check(imageCountResult==VK_SUCCESS && imageResult==VK_SUCCESS && count==1 && image==reinterpret_cast<void*>(0x9200) &&
            windowImageCalls==2 && TryGetSwapchainSnapshot(reinterpret_cast<uintptr_t>(handle),snapshot) &&
            HasObservedSwapchainCreate(snapshot) && snapshot.format==44 && snapshot.width==777 && snapshot.height==555 &&
            snapshot.images.size()==1 && snapshot.images[0]==reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(0x9200)) &&
            fake.createCalls==0 && fake.allocationCalls==0 && fake.submitCalls==0,
            "first table images callbacks retain create metadata and record actual images without renderer work");
#if defined(ESM_PRESENT_DECISION_TRACE)
        VkPresentInfoKHR admissionInfo{};std::uint32_t admissionIndex=0;
        admissionInfo.swapchainCount=1;admissionInfo.pSwapchains=&handle;admissionInfo.pImageIndices=&admissionIndex;
        VkPresentInfoKHR adjustedInfo{};const void* adjustedWaits[8]{};
        MinimapPresentDecision::Trace admission{};
        (void)TrySubmitVulkanMinimap(reinterpret_cast<void*>(0xabcdef),admissionInfo,
            reinterpret_cast<uintptr_t>(handle),0,adjustedInfo,adjustedWaits,false,&admission);
        Check(admission.snapshotKnown==MinimapPresentDecision::Observation::Yes &&
            admission.createProvenance==MinimapPresentDecision::Observation::Yes &&
            admission.deviceMatch==MinimapPresentDecision::Observation::Yes,
            "present admission accepts the exact snapshot captured by zero-status startup hooks");
#else
        Check(TryGetSwapchainSnapshot(reinterpret_cast<uintptr_t>(handle),snapshot) &&
            HasObservedSwapchainCreate(snapshot) && snapshot.device==g_lastVulkanDevice.load(),
            "snapshot admission has exact create provenance and current device after zero-status startup");
#endif
        g_lastConfigPollTick=GetTickCount();g_vulkanFunctionOffsetsLogged=true;presentCalls=0;
        VkPresentInfoKHR info{};info.swapchainCount=1;std::uint32_t index=0;
        info.pSwapchains=&handle;info.pImageIndices=&index;const auto queue=reinterpret_cast<void*>(0xabcdef);
        Check(HookQueuePresent(queue,&info)==-1000001004 && presentCalls==1 && forwardedQueue==queue &&
            forwardedPresent==&info && fake.createCalls==0 && fake.submitCalls==0,
            "activation-window present preserves native input/result and forwarding count");
        ModContext context{};context.Log=[](const char*){};MinimapMod module;
        auto* retained=g_vulkanStartup.hook;
        module.Deactivate(&context);
        Check(!g_runtimeCallbacksActive.load() && !g_vulkanStartup.capture.load() &&
            retained==g_vulkanStartup.hook && IsVulkanStartupOwned(g_vulkanStartup),
            "real module deactivation closes runtime/capture gates while retaining reachable forwarding");
        handle=reinterpret_cast<void*>(0xb200);windowCreateHandle=handle;windowCreateCalls=0;
        Check(HookCreateSwapchain(reinterpret_cast<void*>(0x2000),&create,nullptr,&handle)==VK_SUCCESS &&
            windowCreateCalls==1 && !TryGetSwapchainSnapshot(reinterpret_cast<uintptr_t>(handle),snapshot),
            "deactivated create forwards exactly once without capturing metadata");
    }
    void CheckActivationFailureControls()
    {
        for(int failure=0;failure<2;++failure)
        {
            SetupFakeEnvironment();SetupStartup();StartupGuardFixture guards;
            if(!guards.bytes) return;
            g_runtimeCallbacksActive=false;g_vulkanStartup.capture=false;initResult=0;
            if(failure==0) guards.bytes[0xFE0BE0]^=1;
            else g_vulkanStartup.finalized=false; // Retained post-CAS finalization failure state.
            Check(!ActivateVulkanStartupCallsite() && !g_runtimeCallbacksActive.load() && !g_vulkanStartup.capture.load(),
                "changed guard or failed publication never publishes callback readiness");
            Check(InvokeStartup()==0 && initCalls==1 && g_patchedVulkanDeviceTable.load()==0 &&
                startupTable[0x580/8]==reinterpret_cast<uintptr_t>(&NativeCreate),
                "failed activation retains native forwarding without installing create hooks");
            SetWindowOriginals();auto create=WindowCreateInfo();
            void* handle=reinterpret_cast<void*>(static_cast<uintptr_t>(0xc100+failure));windowCreateHandle=handle;
            SwapchainRuntimeInfo snapshot{};
            Check(HookCreateSwapchain(reinterpret_cast<void*>(0x2000),&create,nullptr,&handle)==VK_SUCCESS &&
                windowCreateCalls==1 && !TryGetSwapchainSnapshot(reinterpret_cast<uintptr_t>(handle),snapshot),
                "failed activation keeps the existing HookCreateSwapchain runtime guard closed");
        }
        g_runtimeCallbacksActive=false;DisableVulkanStartupCapture();
        ModContext context{};context.Log=[](const char*){};MinimapMod notReady;
        notReady.Activate(&context);
        Check(!g_runtimeCallbacksActive.load() && !g_vulkanStartup.capture.load(),
            "module with unproved profile prerequisites cannot publish callback readiness");
        g_client10=false;g_patchedVulkanDeviceTable=0;g_patchedVulkanDevice=0;
    }
}

int main(int argc,char** argv)
{
    if(argc==2 && std::strcmp(argv[1],"--startup-focused")==0)
    {
        SetupFakeEnvironment();CheckRawStartupGuards();CheckStartup();CheckMetadata();
        CheckActivationWindow();CheckActivationFailureControls();
        return failures==0?0:1;
    }
    if(argc==2 && std::strcmp(argv[1],"--activation-window")==0)
    {
        CheckActivationWindow();CheckActivationFailureControls();
        return failures==0?0:1;
    }
    SetupFakeEnvironment();
    constexpr std::uintptr_t unknownSwapchain = 0x9000;
    const auto unknown = MakeImagesOnlySnapshot(unknownSwapchain);
    const auto desktopWidth = static_cast<std::uint32_t>(MaxValue(1, GetSystemMetrics(SM_CXSCREEN)));
    const auto desktopHeight = static_cast<std::uint32_t>(MaxValue(1, GetSystemMetrics(SM_CYSCREEN)));
    const bool unknownBuild = BuildVulkanMinimapRendererLocked(unknown);
    std::cout << "OBS unknown: build=" << unknownBuild
        << " snapshot=" << unknown.width << "x" << unknown.height
        << " format=" << unknown.format << " colorSpace=" << unknown.colorSpace
        << " generation=" << unknown.generation << " images=" << unknown.images.size()
        << " effective=" << g_renderer.width << "x" << g_renderer.height
        << " format=" << g_renderer.format << " createCalls=" << fake.createCalls
        << " commandBufferAllocations=" << fake.allocationCalls
        << " queueSubmitCalls=" << fake.submitCalls << '\n';
    Check(unknown.device == 0x2000 && unknown.handle == unknownSwapchain && unknown.images.size() == 2 &&
        unknown.width == 0 && unknown.height == 0 && unknown.format == 0 && unknown.colorSpace == 0 && unknown.generation == 0,
        "images-only record carries handles and images but no successful create metadata");
    Check(fake.createCalls == 0 && fake.allocationCalls == 0 && fake.submitCalls == 0,
        "unknown create metadata must cause zero renderer resource creation, command-buffer allocation, and submission");
    Check(!unknownBuild, "unknown metadata stays pending without guessed properties");

    SetupFakeEnvironment();
    constexpr std::uintptr_t completeSwapchain = 0xA000;
    VkSwapchainCreateInfoKHR create{};
    create.minImageCount = 3;
    create.imageFormat = 44;
    create.imageColorSpace = 1000104001;
    create.imageExtent = { 777, 555 };
    RememberSwapchainCreate(reinterpret_cast<void*>(0x2000), &create,
        reinterpret_cast<void*>(completeSwapchain), VK_SUCCESS);
    auto complete = MakeImagesOnlySnapshot(completeSwapchain);
    const bool completeBuild = BuildVulkanMinimapRendererLocked(complete);
    std::cout << "OBS complete: build=" << completeBuild
        << " snapshot=" << complete.width << "x" << complete.height
        << " format=" << complete.format << " colorSpace=" << complete.colorSpace
        << " generation=" << complete.generation << " images=" << complete.images.size()
        << " effective=" << g_renderer.width << "x" << g_renderer.height
        << " format=" << g_renderer.format << " renderPassFormat=" << fake.renderPassFormat
        << " imageViewFormat=" << fake.imageViewFormat
        << " framebuffer=" << fake.framebufferWidth << "x" << fake.framebufferHeight
        << " createCalls=" << fake.createCalls << " commandBufferAllocations=" << fake.allocationCalls
        << " queueSubmitCalls=" << fake.submitCalls << '\n';
    Check(completeBuild && complete.width == 777 && complete.height == 555 && complete.format == 44 &&
        complete.colorSpace == 1000104001 && complete.generation != 0 &&
        g_renderer.width == 777 && g_renderer.height == 555 && g_renderer.format == 44 &&
        g_renderer.colorSpace == 1000104001 && fake.renderPassFormat == 44 && fake.imageViewFormat == 44 &&
        fake.framebufferWidth == 777 && fake.framebufferHeight == 555 && fake.createCalls > 0 &&
        fake.allocationCalls > 0 && fake.submitCalls == 0,
        "complete non-default create metadata controls renderer format and extent");

    SetupFakeEnvironment();
    CheckRawStartupGuards();
    CheckStartup();
    CheckMetadata();
    CheckActivationWindow();
    CheckActivationFailureControls();
    return failures == 0 ? 0 : 1;
}
