// Independent offscreen Vulkan readback for the production terrain texture path.
// The expected terrain pixel calculation is copied from the immutable legacy
// DrawRealMap implementation at baseline source SHA
// 51bc3010766fa6d791ecf173eb10c577aa005a22d8b72ea1ab2886544fa71859.
#include "../../src/dllmain.cpp"
#include <iostream>

namespace OffscreenVk {
    using Gipa = void* (__stdcall*)(void*, const char*);
    using Gdpa = void* (__stdcall*)(void*, const char*);
    struct AppInfo { std::uint32_t sType=0; const void* pNext=nullptr; const char* app=nullptr; std::uint32_t appVer=0; const char* engine=nullptr; std::uint32_t engineVer=0; std::uint32_t api=0; };
    struct InstanceInfo { std::uint32_t sType=1; const void* pNext=nullptr; std::uint32_t flags=0; const AppInfo* app=nullptr; std::uint32_t layerCount=0; const char* const* layers=nullptr; std::uint32_t extensionCount=0; const char* const* extensions=nullptr; };
    struct QueueCreateInfo { std::uint32_t sType=2; const void* pNext=nullptr; std::uint32_t flags=0; std::uint32_t family=0; std::uint32_t count=1; const float* priorities=nullptr; };
    struct DeviceCreateInfo { std::uint32_t sType=3; const void* pNext=nullptr; std::uint32_t flags=0; std::uint32_t queueCount=0; const QueueCreateInfo* queues=nullptr; std::uint32_t layerCount=0; const char* const* layers=nullptr; std::uint32_t extensionCount=0; const char* const* extensions=nullptr; const void* features=nullptr; };
    struct QueueFamilyProperties { std::uint32_t flags=0, count=0, timestampBits=0; VkExtent3D granularity{}; };
    struct MemoryType { std::uint32_t flags=0, heapIndex=0; };
    struct MemoryHeap { std::uint64_t size=0; std::uint32_t flags=0; };
    struct MemoryProperties { std::uint32_t typeCount=0; MemoryType types[32]{}; std::uint32_t heapCount=0; MemoryHeap heaps[16]{}; };
    struct FormatProperties { std::uint32_t linear=0, optimal=0, buffer=0; };
    struct PhysicalPropertiesPrefix { std::uint32_t api=0, driver=0, vendor=0, device=0, type=0; char name[256]{}; std::uint8_t uuid[16]{}; };
    struct BufferMemoryBarrier { std::uint32_t sType=44; const void* pNext=nullptr; std::uint32_t srcAccess=0, dstAccess=0, srcFamily=0xffffffffu, dstFamily=0xffffffffu; void* buffer=nullptr; std::uint64_t offset=0, size=~std::uint64_t(0); };
    using CreateInstance = std::int32_t (__stdcall*)(const InstanceInfo*, const void*, void**);
    using DestroyInstance = void (__stdcall*)(void*, const void*);
    using EnumeratePhysicalDevices = std::int32_t (__stdcall*)(void*, std::uint32_t*, void**);
    using GetPhysicalDeviceProperties = void (__stdcall*)(void*, PhysicalPropertiesPrefix*);
    using GetQueueFamilyProperties = void (__stdcall*)(void*, std::uint32_t*, QueueFamilyProperties*);
    using GetMemoryProperties = void (__stdcall*)(void*, MemoryProperties*);
    using GetFormatProperties = void (__stdcall*)(void*, std::uint32_t, FormatProperties*);
    using CreateDevice = std::int32_t (__stdcall*)(void*, const DeviceCreateInfo*, const void*, void**);
    using DestroyDevice = void (__stdcall*)(void*, const void*);
    using GetDeviceQueue = void (__stdcall*)(void*, std::uint32_t, std::uint32_t, void**);
    using QueueWaitIdle = std::int32_t (__stdcall*)(void*);
    using CreateImageView = std::int32_t (__stdcall*)(void*, const VkImageViewCreateInfo*, const void*, void**);
    using DestroyImageView = void (__stdcall*)(void*, void*, const void*);
    using CreateRenderPass = std::int32_t (__stdcall*)(void*, const VkRenderPassCreateInfo*, const void*, void**);
    using DestroyRenderPass = void (__stdcall*)(void*, void*, const void*);
    using CreateFramebuffer = std::int32_t (__stdcall*)(void*, const VkFramebufferCreateInfo*, const void*, void**);
    using DestroyFramebuffer = void (__stdcall*)(void*, void*, const void*);
    using CreateCommandPool = std::int32_t (__stdcall*)(void*, const VkCommandPoolCreateInfo*, const void*, void**);
    using DestroyCommandPool = void (__stdcall*)(void*, void*, const void*);
    using AllocateCommandBuffers = std::int32_t (__stdcall*)(void*, const VkCommandBufferAllocateInfo*, void**);
    using CreateImage = std::int32_t (__stdcall*)(void*, const VkImageCreateInfo*, const void*, void**);
    using DestroyImage = void (__stdcall*)(void*, void*, const void*);
    using CreateBuffer = std::int32_t (__stdcall*)(void*, const VkBufferCreateInfo*, const void*, void**);
    using DestroyBuffer = void (__stdcall*)(void*, void*, const void*);
    using GetMemoryRequirements = void (__stdcall*)(void*, void*, VkMemoryRequirements*);
    using AllocateMemory = std::int32_t (__stdcall*)(void*, const VkMemoryAllocateInfo*, const void*, void**);
    using FreeMemory = void (__stdcall*)(void*, void*, const void*);
    using BindMemory = std::int32_t (__stdcall*)(void*, void*, void*, std::uint64_t);
    using MapMemory = std::int32_t (__stdcall*)(void*, void*, std::uint64_t, std::uint64_t, std::uint32_t, void**);
    using UnmapMemory = void (__stdcall*)(void*, void*);
    using BeginCommandBuffer = std::int32_t (__stdcall*)(void*, const VkCommandBufferBeginInfo*);
    using EndCommandBuffer = std::int32_t (__stdcall*)(void*);
    using CmdBeginRenderPass = void (__stdcall*)(void*, const VkRenderPassBeginInfo*, std::uint32_t);
    using CmdEndRenderPass = void (__stdcall*)(void*);
    using CmdCopyImageToBuffer = void (__stdcall*)(void*, void*, std::uint32_t, void*, std::uint32_t, const VkBufferImageCopy*);
    using CmdPipelineBarrier = void (__stdcall*)(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const void*, std::uint32_t, const BufferMemoryBarrier*, std::uint32_t, const VkImageMemoryBarrier*);
    using QueueSubmit = std::int32_t (__stdcall*)(void*, std::uint32_t, const VkSubmitInfo*, void*);
    using DeviceWaitIdle = std::int32_t (__stdcall*)(void*);

    constexpr std::uint32_t GraphicsQueue = 0x1, HostVisible = 0x2, HostCoherent = 0x4, DeviceLocal = 0x1;
    constexpr std::uint32_t ColorAttachmentFeature = 0x80;
    constexpr std::uint32_t ColorAttachmentUsage = 0x10, TransferSrcUsage = 0x1, TransferDstUsage = 0x2;
    constexpr std::uint32_t BufferTransferDstUsage = 0x2;
    constexpr std::uint32_t StageColorOutput = 0x400, StageTransfer = 0x1000, StageHost = 0x4000;
    constexpr std::uint32_t AccessTransferWrite = 0x1000, AccessHostRead = 0x2000;
    constexpr std::uint32_t LayoutTransferSrc = 6, FormatRgba8 = 37, FormatRgba16f = 97;
    constexpr std::uint32_t FormatFeatureTransferSrc = 0x4000;

    template<class T> T Proc(Gipa gipa, void* instance, const char* name) { return reinterpret_cast<T>(gipa(instance, name)); }
    template<class T> T DevProc(Gdpa gdpa, void* device, const char* name) { return reinterpret_cast<T>(gdpa(device, name)); }
    std::uint32_t FindMemoryType(const MemoryProperties& p, std::uint32_t bits, std::uint32_t flags) {
        for (std::uint32_t i=0;i<p.typeCount;++i) if ((bits & (1u<<i)) && (p.types[i].flags & flags)==flags) return i;
        throw std::runtime_error("no compatible Vulkan memory type");
    }

    std::uint32_t gDraws=0, gUploads=0, gCpuClears=0;
    std::uint64_t gUploadBytes=0;
    float gOracleScale=1; int gOracleCenterX=64, gOracleCenterY=64;
    Gdpa g_activeGdpa=nullptr;
    CmdDrawFn gRealDraw=nullptr;
    CmdCopyBufferToImageFn gRealUpload=nullptr;
    CmdClearAttachmentsFn gRealClear=nullptr;
    CmdPushConstantsFn gRealPush=nullptr;
    float gPush[32]{};
    void __fastcall CountDraw(void* cmd,std::uint32_t vertices,std::uint32_t instances,std::uint32_t first,std::uint32_t firstInstance) {
        ++gDraws; gRealDraw(cmd,vertices,instances,first,firstInstance);
    }
    void __fastcall CountUpload(void* cmd,void* buffer,void* image,std::uint32_t layout,std::uint32_t count,const VkBufferImageCopy* copies) {
        ++gUploads; for(std::uint32_t i=0;i<count;++i)gUploadBytes+=std::uint64_t(copies[i].imageExtent.width)*copies[i].imageExtent.height*4; gRealUpload(cmd,buffer,image,layout,count,copies);
    }
    void __fastcall CountClear(void* cmd,std::uint32_t count,const VkClearAttachment* attachments,std::uint32_t rectCount,const VkClearRect* rects) {
        ++gCpuClears; gRealClear(cmd,count,attachments,rectCount,rects);
    }
    void __fastcall CapturePush(void* cmd,void* layout,std::uint32_t stages,std::uint32_t offset,std::uint32_t size,const void* data) {
        if(offset==0 && size==sizeof(gPush))std::memcpy(gPush,data,size);
        gRealPush(cmd,layout,stages,offset,size,data);
    }
    std::int32_t __fastcall NoSwapchainImages(void*,void*,std::uint32_t*,void*) { return -1; }

    struct Device {
        HMODULE loader=nullptr; Gipa gipa=nullptr; Gdpa gdpa=nullptr;
        void* instance=nullptr; void* physical=nullptr; void* device=nullptr; void* queue=nullptr; void* pool=nullptr; void* command=nullptr;
        MemoryProperties memory{}; QueueWaitIdle waitQueue=nullptr; DeviceWaitIdle waitDevice=nullptr;
        DestroyInstance destroyInstance=nullptr; DestroyDevice destroyDevice=nullptr;
        ~Device() { if(device && waitDevice) waitDevice(device); if(device && destroyDevice) destroyDevice(device,nullptr); if(instance && destroyInstance) destroyInstance(instance,nullptr); if(loader) FreeLibrary(loader); }
        void Initialize() {
            loader=LoadLibraryW(L"vulkan-1.dll"); if(!loader) throw std::runtime_error("Vulkan loader unavailable");
            gipa=reinterpret_cast<Gipa>(GetProcAddress(loader,"vkGetInstanceProcAddr")); if(!gipa) throw std::runtime_error("vkGetInstanceProcAddr unavailable");
            auto createInstance=Proc<CreateInstance>(gipa,nullptr,"vkCreateInstance");
            AppInfo app{}; app.app="MinimapTerrainOffscreen"; app.engine="IndependentVerifier"; app.api=(1u<<22);
            InstanceInfo info{}; info.app=&app;
            if(createInstance(&info,nullptr,&instance)!=VK_SUCCESS) throw std::runtime_error("vkCreateInstance failed");
            destroyInstance=Proc<DestroyInstance>(gipa,instance,"vkDestroyInstance");
            auto enumerate=Proc<EnumeratePhysicalDevices>(gipa,instance,"vkEnumeratePhysicalDevices");
            std::uint32_t count=0; if(enumerate(instance,&count,nullptr)!=VK_SUCCESS || !count) throw std::runtime_error("no Vulkan physical devices");
            std::vector<void*> physicals(count); if(enumerate(instance,&count,physicals.data())!=VK_SUCCESS) throw std::runtime_error("enumerating Vulkan devices failed");
            auto getFamilies=Proc<GetQueueFamilyProperties>(gipa,instance,"vkGetPhysicalDeviceQueueFamilyProperties");
            auto getProps=Proc<GetPhysicalDeviceProperties>(gipa,instance,"vkGetPhysicalDeviceProperties");
            auto createDevice=Proc<CreateDevice>(gipa,instance,"vkCreateDevice");
            auto getMemory=Proc<GetMemoryProperties>(gipa,instance,"vkGetPhysicalDeviceMemoryProperties");
            auto getQueue=Proc<GetDeviceQueue>(gipa,instance,"vkGetDeviceQueue");
            std::uint32_t family=~0u; std::uint8_t propsStorage[1024]{};
            for(void* candidate:physicals) {
                std::uint32_t familyCount=0; getFamilies(candidate,&familyCount,nullptr); std::vector<QueueFamilyProperties> fs(familyCount); getFamilies(candidate,&familyCount,fs.data());
                for(std::uint32_t i=0;i<familyCount;++i) if(fs[i].count && (fs[i].flags&GraphicsQueue)) {
                    physical=candidate; family=i; std::uint8_t candidateProps[1024]{};getProps(candidate,reinterpret_cast<PhysicalPropertiesPrefix*>(candidateProps));
                    if(reinterpret_cast<PhysicalPropertiesPrefix*>(candidateProps)->type==2) break;
                    break;
                }
                if(physical){std::uint8_t candidateProps[1024]{};getProps(physical,reinterpret_cast<PhysicalPropertiesPrefix*>(candidateProps));if(reinterpret_cast<PhysicalPropertiesPrefix*>(candidateProps)->type==2)break;}
            }
            if(!physical) throw std::runtime_error("no graphics-capable Vulkan queue");
            getProps(physical,reinterpret_cast<PhysicalPropertiesPrefix*>(propsStorage)); getMemory(physical,&memory);
            const auto& props=*reinterpret_cast<const PhysicalPropertiesPrefix*>(propsStorage);
            std::cout<<"Vulkan device: "<<props.name<<" (API "<<props.api<<")\n";
            const float priority=1.0f; QueueCreateInfo q{}; q.family=family; q.priorities=&priority;
            DeviceCreateInfo di{}; di.queueCount=1; di.queues=&q;
            if(createDevice(physical,&di,nullptr,&device)!=VK_SUCCESS) throw std::runtime_error("vkCreateDevice failed");
            gdpa=Proc<Gdpa>(gipa,instance,"vkGetDeviceProcAddr");
            destroyDevice=DevProc<DestroyDevice>(gdpa,device,"vkDestroyDevice");
            waitQueue=DevProc<QueueWaitIdle>(gdpa,device,"vkQueueWaitIdle"); waitDevice=DevProc<DeviceWaitIdle>(gdpa,device,"vkDeviceWaitIdle");
            getQueue(device,family,0,&queue);
            auto createPool=DevProc<CreateCommandPool>(gdpa,device,"vkCreateCommandPool");
            auto allocate=DevProc<AllocateCommandBuffers>(gdpa,device,"vkAllocateCommandBuffers");
            VkCommandPoolCreateInfo pi{}; pi.queueFamilyIndex=family;
            if(createPool(device,&pi,nullptr,&pool)!=VK_SUCCESS) throw std::runtime_error("vkCreateCommandPool failed");
            VkCommandBufferAllocateInfo ai{}; ai.commandPool=pool; ai.commandBufferCount=1;
            if(allocate(device,&ai,&command)!=VK_SUCCESS) throw std::runtime_error("vkAllocateCommandBuffers failed");
        }
    };

    struct Target {
        Device& d; std::uint32_t width,height,format; void* image=nullptr; void* imageMemory=nullptr; void* view=nullptr; void* renderPass=nullptr; void* framebuffer=nullptr; void* readback=nullptr; void* readbackMemory=nullptr; void* mapped=nullptr;
        CreateImage createImage; DestroyImage destroyImage; GetMemoryRequirements imageReq; AllocateMemory alloc; FreeMemory freeMem; BindMemory bind; CreateImageView createView; DestroyImageView destroyView; CreateRenderPass createPass; DestroyRenderPass destroyPass; CreateFramebuffer createFb; DestroyFramebuffer destroyFb; CreateBuffer createBuffer; DestroyBuffer destroyBuffer; GetMemoryRequirements bufferReq; MapMemory map; UnmapMemory unmap; CmdCopyImageToBuffer copyImage;
        Target(Device& device,std::uint32_t w,std::uint32_t h,std::uint32_t fmt):d(device),width(w),height(h),format(fmt) {
            auto g=d.gdpa; auto dev=d.device;
            createImage=DevProc<CreateImage>(g,dev,"vkCreateImage"); destroyImage=DevProc<DestroyImage>(g,dev,"vkDestroyImage"); imageReq=DevProc<GetMemoryRequirements>(g,dev,"vkGetImageMemoryRequirements");
            alloc=DevProc<AllocateMemory>(g,dev,"vkAllocateMemory"); freeMem=DevProc<FreeMemory>(g,dev,"vkFreeMemory"); bind=DevProc<BindMemory>(g,dev,"vkBindImageMemory");
            createView=DevProc<CreateImageView>(g,dev,"vkCreateImageView"); destroyView=DevProc<DestroyImageView>(g,dev,"vkDestroyImageView"); createPass=DevProc<CreateRenderPass>(g,dev,"vkCreateRenderPass"); destroyPass=DevProc<DestroyRenderPass>(g,dev,"vkDestroyRenderPass"); createFb=DevProc<CreateFramebuffer>(g,dev,"vkCreateFramebuffer"); destroyFb=DevProc<DestroyFramebuffer>(g,dev,"vkDestroyFramebuffer");
            createBuffer=DevProc<CreateBuffer>(g,dev,"vkCreateBuffer"); destroyBuffer=DevProc<DestroyBuffer>(g,dev,"vkDestroyBuffer"); bufferReq=DevProc<GetMemoryRequirements>(g,dev,"vkGetBufferMemoryRequirements"); map=DevProc<MapMemory>(g,dev,"vkMapMemory"); unmap=DevProc<UnmapMemory>(g,dev,"vkUnmapMemory"); copyImage=DevProc<CmdCopyImageToBuffer>(g,dev,"vkCmdCopyImageToBuffer");
            auto bindBuffer=DevProc<BindMemory>(g,dev,"vkBindBufferMemory");
            VkImageCreateInfo ii{}; ii.format=format; ii.extent={w,h,1}; ii.usage=ColorAttachmentUsage|TransferSrcUsage;
            if(createImage(dev,&ii,nullptr,&image)!=VK_SUCCESS) throw std::runtime_error("create offscreen image failed");
            VkMemoryRequirements mr{}; imageReq(dev,image,&mr); VkMemoryAllocateInfo mai{}; mai.allocationSize=mr.size; mai.memoryTypeIndex=FindMemoryType(d.memory,mr.memoryTypeBits,0);
            if(alloc(dev,&mai,nullptr,&imageMemory)!=VK_SUCCESS || bind(dev,image,imageMemory,0)!=VK_SUCCESS) throw std::runtime_error("allocate/bind offscreen image failed");
            VkImageViewCreateInfo vi{}; vi.image=image; vi.format=format; vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount=1; vi.subresourceRange.layerCount=1;
            if(createView(dev,&vi,nullptr,&view)!=VK_SUCCESS) throw std::runtime_error("create offscreen view failed");
            VkAttachmentDescription attachment{}; attachment.format=format; attachment.loadOp=1; attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE; attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout=LayoutTransferSrc;
            VkAttachmentReference color{}; color.attachment=0; color.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkSubpassDescription subpass{}; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&color;
            VkRenderPassCreateInfo ri{}; ri.attachmentCount=1; ri.pAttachments=&attachment; ri.subpassCount=1; ri.pSubpasses=&subpass;
            if(createPass(dev,&ri,nullptr,&renderPass)!=VK_SUCCESS) throw std::runtime_error("create offscreen render pass failed");
            VkFramebufferCreateInfo fi{}; fi.renderPass=renderPass; fi.attachmentCount=1; fi.pAttachments=&view; fi.width=w; fi.height=h;
            if(createFb(dev,&fi,nullptr,&framebuffer)!=VK_SUCCESS) throw std::runtime_error("create offscreen framebuffer failed");
            VkBufferCreateInfo bi{}; bi.size=std::uint64_t(w)*h*(format==FormatRgba8?4:8); bi.usage=BufferTransferDstUsage;
            if(createBuffer(dev,&bi,nullptr,&readback)!=VK_SUCCESS) throw std::runtime_error("create readback buffer failed");
            bufferReq(dev,readback,&mr); mai.allocationSize=mr.size; mai.memoryTypeIndex=FindMemoryType(d.memory,mr.memoryTypeBits,HostVisible|HostCoherent);
            if(alloc(dev,&mai,nullptr,&readbackMemory)!=VK_SUCCESS || bindBuffer(dev,readback,readbackMemory,0)!=VK_SUCCESS) throw std::runtime_error("allocate/bind readback buffer failed");
            if(map(dev,readbackMemory,0,bi.size,0,&mapped)!=VK_SUCCESS || !mapped) throw std::runtime_error("map readback buffer failed");
        }
        ~Target() {
            if(mapped) unmap(d.device,readbackMemory);
            if(readback) destroyBuffer(d.device,readback,nullptr); if(readbackMemory) freeMem(d.device,readbackMemory,nullptr);
            if(framebuffer) destroyFb(d.device,framebuffer,nullptr); if(renderPass) destroyPass(d.device,renderPass,nullptr);
            if(view) destroyView(d.device,view,nullptr); if(image) destroyImage(d.device,image,nullptr); if(imageMemory) freeMem(d.device,imageMemory,nullptr);
        }
    };

    void* __fastcall DeviceProcShim(void* device,const char* name) {
        if(std::strcmp(name,"vkGetSwapchainImagesKHR")==0) return reinterpret_cast<void*>(&NoSwapchainImages);
        return g_activeGdpa ? g_activeGdpa(device,name) : nullptr;
    }

    struct Rgba { float r=0,g=0,b=0,a=1; };
    float Clamp(float v,float lo,float hi){return (std::max)(lo,(std::min)(hi,v));}
    Rgba Encode(Rgba c,std::uint32_t space) {
        if(space==1000104002u || space==1000104008u) {
            auto linear=[](float v){v=Clamp(v,0,1);return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
            c.r=linear(c.r);c.g=linear(c.g);c.b=linear(c.b);
            if(space==1000104002u){c.r*=203.f/80;c.g*=203.f/80;c.b*=203.f/80;return c;}
            const float r=.627404f*c.r+.329283f*c.g+.043313f*c.b, g=.069097f*c.r+.919540f*c.g+.011362f*c.b, b=.016391f*c.r+.088013f*c.g+.895595f*c.b;
            auto pq=[](float v){float y=std::pow((std::max)(0.f,v)*203.f/10000.f,2610.f/16384.f);return std::pow((3424.f/4096.f+(2413.f/128.f)*y)/(1.f+(2392.f/128.f)*y),2523.f/32.f);};
            c.r=pq(r);c.g=pq(g);c.b=pq(b);
        }
        return c;
    }
    // CPU oracle formulas are transcribed from the immutable legacy baseline
    // dllmain.cpp SHA256 51bc3010766fa6d791ecf173eb10c577aa005a22d8b72ea1ab2886544fa71859:
    // SampleLiveFog, ApplyExplorationFog, ApplyMapLighting, and DrawRealMap.
    float FogAt(const LiveFogSnapshot& fog,float x,float z) {
        if(x<0||z<0||x>fog.worldWidth||z>fog.worldHeight)return 0;
        const float fx=Clamp(x*.125f-.5f,0.f,float(fog.columns*32-1)), fy=Clamp((fog.worldHeight-z)*.125f-.5f,0.f,float(fog.rows*32-1));
        const auto x0=std::uint32_t(fx),y0=std::uint32_t(fy),x1=(std::min)(x0+1,fog.columns*32-1),y1=(std::min)(y0+1,fog.rows*32-1);
        const float tx=fx-x0,ty=fy-y0;
        auto sample=[&](std::uint32_t xx,std::uint32_t yy){auto index=(std::size_t(yy/32)*fog.columns+xx/32)*1024+(yy%32)*32+xx%32;return fog.tiles[index]/255.f;};
        return (sample(x0,y0)*(1-tx)+sample(x1,y0)*tx)*(1-ty)+(sample(x0,y1)*(1-tx)+sample(x1,y1)*tx)*ty;
    }
    Rgba LegacyPixel(const RealMapTexture& map,const LiveFogSnapshot* fog,bool fogEnabled,int localX,int localY,int radius,float centerX,float centerZ,float units,float heading,int light,std::uint32_t space,Rgba* preQuantized=nullptr,std::array<float,3>* quantMargins=nullptr) {
        const float co=std::cos(heading),si=std::sin(heading); const float nx=co*localX-si*localY, ny=si*localX+co*localY;
        const float worldX=centerX+nx*units, worldZ=centerZ-ny*units;
        const float u=Clamp(worldX/REAL_MAP_WORLD_SIZE,0,1),v=Clamp(1-worldZ/REAL_MAP_WORLD_SIZE,0,1);
        const float fx=u*(map.width-1),fy=v*(map.height-1); const int x0=int(std::floor(fx)),y0=int(std::floor(fy)),x1=(std::min)(x0+1,map.width-1),y1=(std::min)(y0+1,map.height-1); const float tx=fx-x0,ty=fy-y0;
        auto ch=[&](int x,int y,int c){return map.rgba[(std::size_t(y)*map.width+x)*4+c]/255.f;};
        auto mix=[](float a,float b,float t){return a*(1-t)+b*t;};
        Rgba c{mix(mix(ch(x0,y0,0),ch(x1,y0,0),tx),mix(ch(x0,y1,0),ch(x1,y1,0),tx),ty),mix(mix(ch(x0,y0,1),ch(x1,y0,1),tx),mix(ch(x0,y1,1),ch(x1,y1,1),tx),ty),mix(mix(ch(x0,y0,2),ch(x1,y0,2),tx),mix(ch(x0,y1,2),ch(x1,y1,2),tx),ty),1};
        c.r=Clamp((c.r-.42f)*1.20f+.42f,0,1);c.g=Clamp((c.g-.42f)*1.22f+.42f,0,1);c.b=Clamp((c.b-.42f)*1.16f+.42f,0,1);
        const float strength=Clamp(float(light),0,100)/100.f,lift=strength*.62f,style=(std::min)(1.f,strength/.55f); c.r+=(1-c.r)*lift;c.g=(c.g+(1-c.g)*lift)*(1-.035f*style);c.b=(c.b+(1-c.b)*lift)*(1-.115f*style);
        const float distance=std::sqrt(float(localX*localX+localY*localY))/radius;const float vignette=(1.02f-.01f*style)-(.34f-.24f*style)*distance*distance;c.r=Clamp(c.r*vignette+.004f,0,1);c.g=Clamp(c.g*vignette+.004f,0,1);c.b=Clamp(c.b*vignette+.004f,0,1);
        if(fogEnabled){float reveal=fog?FogAt(*fog,worldX,worldZ):0;c.r=.30f+(c.r-.30f)*reveal;c.g=.31f+(c.g-.31f)*reveal;c.b=.28f+(c.b-.28f)*reveal;}
        const float levels=light==0?34.f:48.f;if(preQuantized)*preQuantized=c;if(quantMargins){const float values[3]={c.r,c.g,c.b};for(int i=0;i<3;++i){float scaled=Clamp(values[i],0,1)*levels;(*quantMargins)[i]=std::fabs((scaled-std::floor(scaled))-.5f)/levels;}}c.r=std::round(Clamp(c.r,0,1)*levels)/levels;c.g=std::round(Clamp(c.g,0,1)*levels)/levels;c.b=std::round(Clamp(c.b,0,1)*levels)/levels;
        return Encode(c,space);
    }
    std::vector<Rgba> LegacyFrame(const RealMapTexture& map,const LiveFogSnapshot* fog,bool fogEnabled,int width,int height,int cx,int cy,int radius,int step,float centerX,float centerZ,float units,float heading,int light,std::uint32_t space,Rgba clear,std::vector<Rgba>* preQuantized=nullptr,std::vector<std::array<float,3>>* quantMargins=nullptr) {
        std::vector<Rgba> out(std::size_t(width)*height,clear);const int left=cx-radius,top=cy-radius,side=2*radius+1;
        if(preQuantized)preQuantized->assign(std::size_t(width)*height,Rgba{});if(quantMargins)quantMargins->assign(std::size_t(width)*height,std::array<float,3>{1.f,1.f,1.f});for(int by=0;by<side;by+=step)for(int bx=0;bx<side;bx+=step){int bw=(std::min)(step,side-bx),bh=(std::min)(step,side-by);int sx=bx+bw/2-radius,sy=by+bh/2-radius;if(sx*sx+sy*sy>radius*radius)continue;Rgba raw{};std::array<float,3> margins{};Rgba c=LegacyPixel(map,fog,fogEnabled,sx,sy,radius,centerX,centerZ,units,heading,light,space,&raw,&margins);// Geometry is the frozen CPU lround contract, independently rasterized before clipping.
        auto edge=[](int value,int center){return int(std::lround(center+(value-center)*gOracleScale));};
        const int x0=edge(left+bx,gOracleCenterX),x1=edge(left+bx+bw,gOracleCenterX);
        const int y0=edge(top+by,gOracleCenterY),y1=edge(top+by+bh,gOracleCenterY);
        for(int py=y0;py<y1;++py)for(int px=x0;px<x1;++px){if(px>=0&&py>=0&&px<width&&py<height){auto index=std::size_t(py)*width+px;out[index]=c;if(preQuantized)(*preQuantized)[index]=raw;if(quantMargins)(*quantMargins)[index]=margins;}}}
        return out;
    }
    float MaxRgbError(const Rgba& a,const Rgba& b){return (std::max)({std::fabs(a.r-b.r),std::fabs(a.g-b.g),std::fabs(a.b-b.b)});}
    bool MatchesAdjacentQuantizationBins(const Rgba& actual,const Rgba& raw,const std::array<float,3>& margins,float levels,std::uint32_t colorSpace,float tolerance){constexpr float boundaryEpsilon=2e-6f;const bool boundary[3]={margins[0]<=boundaryEpsilon,margins[1]<=boundaryEpsilon,margins[2]<=boundaryEpsilon};int count=0;for(bool b:boundary)count+=b?1:0;if(count==0)return false;const float values[3]={raw.r,raw.g,raw.b};for(int mask=0;mask<(1<<count);++mask){Rgba candidate{0,0,0,1};int bit=0;float* channels[3]={&candidate.r,&candidate.g,&candidate.b};for(int channel=0;channel<3;++channel){float scaled=Clamp(values[channel],0,1)*levels;float lower=std::floor(scaled);float bin=std::round(scaled);if(boundary[channel]){bin=((mask>>bit)&1)?lower+1.f:lower;++bit;}*channels[channel]=bin/levels;}candidate=Encode(candidate,colorSpace);if(MaxRgbError(actual,candidate)<=tolerance+1e-6f)return true;}return false;}
    float HalfToFloat(std::uint16_t h) {
        const std::uint32_t sign=std::uint32_t(h&0x8000)<<16;std::uint32_t e=(h>>10)&31,m=h&1023,bits=0;
        if(e==0){if(!m)bits=sign;else{e=1;while(!(m&1024)){m<<=1;--e;}m&=1023;bits=sign|((e+112)<<23)|(m<<13);}}
        else if(e==31)bits=sign|0x7f800000|(m<<13);else bits=sign|((e+112)<<23)|(m<<13);
        float f;std::memcpy(&f,&bits,4);return f;
    }
    bool gImageFailed=false;
    void Check(bool okay,const std::string& message){if(!okay){std::cerr<<"FAIL: "<<message<<"\n";std::exit(1);}std::cout<<"PASS: "<<message<<"\n";}
    void CheckImage(bool okay,const std::string& message){if(!okay){gImageFailed=true;std::cerr<<"FAIL: "<<message<<"\n";return;}std::cout<<"PASS: "<<message<<"\n";}
}

int main(int argc,char** argv) {
    const bool fogOnly=argc>1 && std::strcmp(argv[1],"--fog-only")==0;
    using namespace OffscreenVk;
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    try {
        Device d; d.Initialize();
        auto getFormat=Proc<GetFormatProperties>(d.gipa,d.instance,"vkGetPhysicalDeviceFormatProperties");
        const std::uint32_t formats[]={FormatRgba8,FormatRgba16f};
        for(auto fmt:formats){FormatProperties fp{};getFormat(d.physical,fmt,&fp);if((fp.optimal&ColorAttachmentFeature)==0)throw std::runtime_error(fmt==FormatRgba16f?"RGBA16F color attachment unsupported: scRGB/PQ image verification unavailable":"RGBA8 UNORM color attachment unsupported");}
        constexpr int W=128,H=128,R=31,CX=64,CY=64,MapW=256,MapH=256;
        const int fogCells=40; auto fog=std::make_shared<LiveFogSnapshot>();fog->columns=fog->rows=fogCells;fog->worldWidth=fog->worldHeight=REAL_MAP_WORLD_SIZE;fog->tick=GetTickCount();fog->session=g_worldSessionGeneration.load();fog->tiles.resize(std::size_t(fogCells)*fogCells*1024);
        for(std::size_t i=0;i<fog->tiles.size();++i)fog->tiles[i]=std::uint8_t((i*73+i/13)%256);
        g_realMap={};g_realMap.attempted=g_realMap.loaded=true;g_realMap.width=MapW;g_realMap.height=MapH;g_realMap.rgba.resize(std::size_t(MapW)*MapH*4);
        for(int y=0;y<MapH;++y)for(int x=0;x<MapW;++x){auto o=(std::size_t(y)*MapW+x)*4;g_realMap.rgba[o]=std::uint8_t((x*5+y*3)%256);g_realMap.rgba[o+1]=std::uint8_t((x*2+y*7+41)%256);g_realMap.rgba[o+2]=std::uint8_t((x*11+y*13+17)%256);g_realMap.rgba[o+3]=255;}
        g_defaultMap={};g_defaultMap.attempted=g_defaultMap.loaded=true;g_defaultMap.width=512;g_defaultMap.height=512;g_defaultMap.rgba.resize(std::size_t(512)*512*4);
        for(int y=0;y<512;++y)for(int x=0;x<512;++x){auto o=(std::size_t(y)*512+x)*4;g_defaultMap.rgba[o]=std::uint8_t((x*3+y*9+21)%256);g_defaultMap.rgba[o+1]=std::uint8_t((x*17+y*2+5)%256);g_defaultMap.rgba[o+2]=std::uint8_t((x*7+y*5+71)%256);g_defaultMap.rgba[o+3]=255;}
        g_minimapMapLight=55; g_minimapMapSampleStep=1; g_defaultMapStyle=false;
        const struct Case{std::uint32_t format,colorSpace;const char* name;float tolerance;} cases[]={{FormatRgba8,0,"SDR",2.f/255.f},{FormatRgba16f,1000104002u,"scRGB",.002f},{FormatRgba16f,1000104008u,"PQ raw encoding",.002f}};
        for(const auto& targetCase:cases) {
            fog->session=g_worldSessionGeneration;fog->tick=GetTickCount();
            g_defaultMapStyle=false;
            Target target(d,W,H,targetCase.format);
            VulkanMinimapRenderer renderer{};renderer.device=reinterpret_cast<std::uintptr_t>(d.device);renderer.width=W;renderer.height=H;renderer.renderPass=reinterpret_cast<std::uintptr_t>(target.renderPass);renderer.colorSpace=targetCase.colorSpace;renderer.commandBuffers={reinterpret_cast<std::uintptr_t>(d.command)};renderer.fns={};
            g_activeGdpa=d.gdpa;
            const bool fnsLoaded=LoadVulkanRendererFnsWithResolver(renderer.fns,d.device,reinterpret_cast<GetDeviceProcAddrFn>(&DeviceProcShim));
            if(!fnsLoaded){const auto& f=renderer.fns;std::cerr<<"missing fns: "<<f.getSwapchainImages<<" "<<f.createImageView<<" "<<f.createRenderPass<<" "<<f.createFramebuffer<<" "<<f.createCommandPool<<" "<<f.allocateCommandBuffers<<" "<<f.resetCommandBuffer<<" "<<f.beginCommandBuffer<<" "<<f.endCommandBuffer<<" "<<f.cmdBeginRenderPass<<" "<<f.cmdEndRenderPass<<" "<<f.cmdClearAttachments<<" "<<f.createSemaphore<<" "<<f.destroySemaphore<<" "<<f.createFence<<" "<<f.destroyFence<<" "<<f.getFenceStatus<<" "<<f.resetFences<<" "<<f.queueSubmit<<" "<<f.deviceWaitIdle<<"\n";}
            Check(fnsLoaded,std::string(targetCase.name)+" renderer Vulkan functions resolved");
            gRealDraw=renderer.fns.cmdDraw; gRealUpload=renderer.fns.cmdCopyBufferToImage; gRealClear=renderer.fns.cmdClearAttachments;gRealPush=renderer.fns.cmdPushConstants;
            renderer.fns.cmdDraw=&CountDraw;renderer.fns.cmdCopyBufferToImage=&CountUpload;renderer.fns.cmdClearAttachments=&CountClear;renderer.fns.cmdPushConstants=&CapturePush;
            renderer.terrainPrepared=true;
            auto reset=DevProc<ResetCommandBufferFn>(d.gdpa,d.device,"vkResetCommandBuffer");auto begin=DevProc<BeginCommandBuffer>(d.gdpa,d.device,"vkBeginCommandBuffer");auto end=DevProc<EndCommandBuffer>(d.gdpa,d.device,"vkEndCommandBuffer");auto beginPass=DevProc<CmdBeginRenderPass>(d.gdpa,d.device,"vkCmdBeginRenderPass");auto endPass=DevProc<CmdEndRenderPass>(d.gdpa,d.device,"vkCmdEndRenderPass");auto copy=target.copyImage;auto barrier=DevProc<CmdPipelineBarrier>(d.gdpa,d.device,"vkCmdPipelineBarrier");auto submit=DevProc<QueueSubmit>(d.gdpa,d.device,"vkQueueSubmit");
            int viewX=CX,viewY=CY;
            Rgba clear=targetCase.format==FormatRgba8?Rgba{7/255.f,11/255.f,19/255.f,1}:Rgba{.125f,.25f,.375f,1};
            auto render=[&](int step,float cx,float cz,float units,float heading,bool fogEnabled,const std::shared_ptr<const LiveFogSnapshot>& f,bool expectValidFog=true,bool refreshFog=true){
                g_minimapMapSampleStep=step;MinimapFogFrame fogFrame{fogEnabled,f};
                Check(reset(d.command,0)==VK_SUCCESS,"reset offscreen command buffer");
                VkCommandBufferBeginInfo beginInfo{};
                Check(begin(d.command,&beginInfo)==VK_SUCCESS,"begin offscreen command buffer");
                if(f && refreshFog)const_cast<LiveFogSnapshot*>(f.get())->tick=GetTickCount();
                const DWORD prepareStart=GetTickCount();Check(PrepareTerrainTexture(renderer,0,d.command,fogFrame),std::string(targetCase.name)+" terrain texture prepared");if(f && refreshFog){auto* snapshot=const_cast<LiveFogSnapshot*>(f.get());snapshot->tick=GetTickCount();std::cerr<<"fog-fixture-session="<<snapshot->session<<" generation="<<g_worldSessionGeneration.load()<<" age-ms="<<(GetTickCount()-snapshot->tick)<<" prepare-ms="<<(GetTickCount()-prepareStart)<<"\\n";}
                VkClearValue cv{};cv.color.float32[0]=clear.r;cv.color.float32[1]=clear.g;cv.color.float32[2]=clear.b;cv.color.float32[3]=1;
                VkRenderPassBeginInfo rbi{};rbi.renderPass=target.renderPass;rbi.framebuffer=target.framebuffer;rbi.renderArea.extent={W,H};rbi.clearValueCount=1;rbi.pClearValues=&cv;beginPass(d.command,&rbi,0);
                DrawRealMap(renderer,d.command,viewX,viewY,R,cx,cz,units,heading,fogFrame);endPass(d.command);
                VkImageMemoryBarrier imageBarrier{};imageBarrier.srcAccessMask=0x100;imageBarrier.dstAccessMask=0x800;imageBarrier.oldLayout=LayoutTransferSrc;imageBarrier.newLayout=LayoutTransferSrc;imageBarrier.image=target.image;imageBarrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;imageBarrier.subresourceRange.levelCount=1;imageBarrier.subresourceRange.layerCount=1;
                barrier(d.command,StageColorOutput,StageTransfer,0,0,nullptr,0,nullptr,1,&imageBarrier);
                VkBufferImageCopy region{};region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;region.imageSubresource.layerCount=1;region.imageExtent={W,H,1};
                copy(d.command,target.image,LayoutTransferSrc,target.readback,1,&region);
                BufferMemoryBarrier host{};host.srcAccess=AccessTransferWrite;host.dstAccess=AccessHostRead;host.buffer=target.readback;host.size=std::uint64_t(W)*H*(targetCase.format==FormatRgba8?4:8);
                barrier(d.command,StageTransfer,StageHost,0,0,nullptr,1,&host,0,nullptr);
                Check(end(d.command)==VK_SUCCESS,"end offscreen command buffer");VkSubmitInfo si{};si.commandBufferCount=1;si.pCommandBuffers=reinterpret_cast<const void* const*>(&d.command);Check(submit(d.queue,1,&si,nullptr)==VK_SUCCESS,"submit offscreen terrain draw");Check(d.waitQueue(d.queue)==VK_SUCCESS,"wait for offscreen readback");
                std::vector<Rgba> got(std::size_t(W)*H);if(targetCase.format==FormatRgba8){auto b=static_cast<const std::uint8_t*>(target.mapped);for(std::size_t i=0;i<got.size();++i)got[i]={b[i*4]/255.f,b[i*4+1]/255.f,b[i*4+2]/255.f,b[i*4+3]/255.f};}
                else {auto h=static_cast<const std::uint16_t*>(target.mapped);for(std::size_t i=0;i<got.size();++i)got[i]={HalfToFloat(h[i*4]),HalfToFloat(h[i*4+1]),HalfToFloat(h[i*4+2]),HalfToFloat(h[i*4+3])};}
                const RealMapTexture& active=g_defaultMapStyle.load()?g_defaultMap:g_realMap;std::vector<Rgba> preQuantized;std::vector<std::array<float,3>> quantMargins;const float mapLevels=g_minimapMapLight.load()==0?34.f:48.f;auto expected=LegacyFrame(active,expectValidFog?f.get():nullptr,fogEnabled,W,H,viewX,viewY,R,step,cx,cz,units,heading,g_minimapMapLight.load(),targetCase.colorSpace,clear,&preQuantized,&quantMargins);if(step==1&&cx==5120&&cz==5120){const auto probe=std::size_t(52)*W+69;std::cerr<<"valid-fixture-probe=(69,52) actual="<<got[probe].r<<","<<got[probe].g<<","<<got[probe].b<<" legacy="<<expected[probe].r<<","<<expected[probe].g<<","<<expected[probe].b<<" session="<<(f?f->session:9999)<<" generation="<<g_worldSessionGeneration.load()<<"\\n";}
                float maxError=0,maxUnacceptedError=0;std::size_t maxAt=0,maxUnacceptedAt=0,boundaryAccepted=0,firstBoundaryAt=0;for(std::size_t i=0;i<got.size();++i){float e=MaxRgbError(got[i],expected[i]);if(e>maxError){maxError=e;maxAt=i;}if(e>targetCase.tolerance+1e-6f){if(MatchesAdjacentQuantizationBins(got[i],preQuantized[i],quantMargins[i],mapLevels,targetCase.colorSpace,targetCase.tolerance)){if(boundaryAccepted==0)firstBoundaryAt=i;++boundaryAccepted;}else if(e>maxUnacceptedError){maxUnacceptedError=e;maxUnacceptedAt=i;}}}
                if(maxUnacceptedError>targetCase.tolerance+1e-6f)std::cerr<<"unaccepted-at=("<<maxUnacceptedAt%W<<","<<maxUnacceptedAt/W<<") actual="<<got[maxUnacceptedAt].r<<","<<got[maxUnacceptedAt].g<<","<<got[maxUnacceptedAt].b<<" expected="<<expected[maxUnacceptedAt].r<<","<<expected[maxUnacceptedAt].g<<","<<expected[maxUnacceptedAt].b<<" raw="<<preQuantized[maxUnacceptedAt].r<<","<<preQuantized[maxUnacceptedAt].g<<","<<preQuantized[maxUnacceptedAt].b<<" draw="<<gDraws<<" upload="<<gUploads<<" clear="<<gCpuClears<<" qmargin="<<quantMargins[maxUnacceptedAt][0]<<","<<quantMargins[maxUnacceptedAt][1]<<","<<quantMargins[maxUnacceptedAt][2]<<" push="<<gPush[0]<<","<<gPush[1]<<","<<gPush[2]<<","<<gPush[3]<<","<<gPush[4]<<","<<gPush[5]<<","<<gPush[6]<<","<<gPush[7]<<","<<gPush[20]<<","<<gPush[21]<<","<<gPush[22]<<"\n";
                if(boundaryAccepted>0)std::cerr<<"explicit adjacent-bin acceptance count="<<boundaryAccepted<<" first=("<<firstBoundaryAt%W<<","<<firstBoundaryAt/W<<") margins="<<quantMargins[firstBoundaryAt][0]<<","<<quantMargins[firstBoundaryAt][1]<<","<<quantMargins[firstBoundaryAt][2]<<"\n";CheckImage(maxUnacceptedError<=targetCase.tolerance+1e-6f,std::string(targetCase.name)+" readback matches legacy image or exact-boundary adjacent bins; raw max error="+std::to_string(maxError)+" unaccepted error="+std::to_string(maxUnacceptedError)+" boundary bins="+std::to_string(boundaryAccepted));
                bool untouched=true;
                for(std::size_t i=0;i<got.size();++i)if(quantMargins[i]==std::array<float,3>{1,1,1})
                    untouched &= got[i].r==clear.r && got[i].g==clear.g && got[i].b==clear.b;
                Check(untouched,std::string(targetCase.name)+" all uncovered and clipped pixels retain exact clear colour");
                Check(gDraws==1 && gCpuClears==0,std::string(targetCase.name)+" visible terrain records one GPU draw and no per-pixel CPU clear path");
                return got;
            };
            gDraws=gUploads=gCpuClears=0;
            render(1,5120,5120,7,0,true,fog);CheckImage(gUploads==1,"static atlas uploads once for initialized slot; uploads="+std::to_string(gUploads));
            for(int step=1;step<=4;++step)for(int rot=0;rot<4;++rot){gDraws=0;render(step,4800+rot*100,5250-rot*80,5+step,rot*1.57079632679f,true,fog);CheckImage(gUploads==1,"movement/rotation/sample-step does not reupload static atlas; uploads="+std::to_string(gUploads));}
            if(!fogOnly) for(int percent:{50,75,100,125,150,175,200}) {
                g_minimapScalePercent=percent; renderer.widgetScale=gOracleScale=percent/100.f;
                renderer.widgetCenterX=gOracleCenterX=CX; renderer.widgetCenterY=gOracleCenterY=CY;
                for(int step=1;step<=4;++step)for(float heading:{0.f,.37f}) {
                    gDraws=0;render(step,4800,5250,5+step,heading,true,fog);
                }
            }
            if(!fogOnly) for(int percent:{50,75,100,125,150,175,200}) {
                g_minimapScalePercent=percent;renderer.widgetScale=gOracleScale=percent/100.f;
                for(int edge:{-4,W-3}) {
                    viewX=viewY=edge;
                    renderer.widgetCenterX=gOracleCenterX=edge;
                    renderer.widgetCenterY=gOracleCenterY=edge;
                    for(int step=1;step<=4;++step){gDraws=0;render(step,5100,5100,6,.37f,true,fog);}
                }
                viewX=CX;viewY=CY;
                renderer.widgetCenterX=gOracleCenterX=CX;renderer.widgetCenterY=gOracleCenterY=CY;
                for(int light:{0,100}){g_minimapMapLight=light;gDraws=0;render(3,-10,10250,6,.37f,false,nullptr);}
                g_minimapMapLight=55;
            }
            g_minimapScalePercent=100;renderer.widgetScale=gOracleScale=1;
            const auto northUp=ResolveMinimapOrientationAngles(MinimapOrientation::NorthUp,1.57079632679f,true);
            gDraws=0;render(2,4800,5250,7,northUp.mapHeading,true,fog);
            CheckImage(std::fabs(gPush[8]-1.0f)<1e-6f&&std::fabs(gPush[9])<1e-6f,
                "north-up sends a fixed-north rotation to the shared GPU terrain path");
            CheckImage(gUploads==1,"north-up movement keeps the static GPU atlas resident");
            auto equalFog=std::make_shared<LiveFogSnapshot>(*fog);gDraws=0;
            const auto equalBytes=gUploadBytes;render(1,5120,5120,7,0,true,equalFog);
            CheckImage(gUploadBytes==equalBytes,"identical per-slot fog bytes require no upload");
            // Each 32x32 tile occupies 4096 RGBA bytes even when only its red channel is consumed.
            const auto dirtyBytes=gUploadBytes;
            auto changedFog=std::make_shared<LiveFogSnapshot>(*fog);std::fill(changedFog->tiles.begin()+820*1024,changedFog->tiles.begin()+821*1024,0);gDraws=0;render(3,5100,5100,6,.37f,true,changedFog);CheckImage(gUploads==2,"changed fog snapshot performs one partial upload after initialization; uploads="+std::to_string(gUploads));
            CheckImage(gUploadBytes-dirtyBytes==4096,"one changed fog tile uploads only 4096 bytes; bytes="+std::to_string(gUploadBytes-dirtyBytes));
            g_defaultMapStyle=true;gDraws=0;render(2,5100,5100,6,.37f,false,nullptr);CheckImage(gUploads==3,"switching Original/HD map refreshes atlas once; uploads="+std::to_string(gUploads));
            changedFog->tick=GetTickCount()-3000;gDraws=0;
            render(3,5100,5100,6,.37f,true,changedFog,false,false);
            ++g_worldSessionGeneration;changedFog->tick=GetTickCount();gDraws=0;
            render(3,5100,5100,6,.37f,true,changedFog,false);
            auto sessionFog=std::make_shared<LiveFogSnapshot>(*changedFog);sessionFog->session=g_worldSessionGeneration;
            gDraws=0;render(3,5100,5100,6,.37f,true,sessionFog);
            auto multiFog=std::make_shared<LiveFogSnapshot>(*sessionFog);
            std::fill(multiFog->tiles.begin()+819*1024,multiFog->tiles.begin()+820*1024,255);
            std::fill(multiFog->tiles.begin()+860*1024,multiFog->tiles.begin()+861*1024,0);
            const auto multiBytes=gUploadBytes;gDraws=0;render(4,5100,5100,6,.37f,true,multiFog);
            CheckImage(gUploadBytes-multiBytes==8192,"two separated dirty fog tiles upload 8192 bytes with unchanged GPU pixels elsewhere");
            d.waitQueue(d.queue);DestroyTexturedResources(renderer,renderer.terrainSlots[0]);renderer.terrainSlots.clear();
            std::cout<<"PASS: "<<targetCase.name<<" GPU readback matrix completed\n";
        }
    } catch(const std::exception& e) { std::cerr<<"UNAVAILABLE/FAIL: "<<e.what()<<"\n"; return 2; }
    return gImageFailed ? 1 : 0;
}
