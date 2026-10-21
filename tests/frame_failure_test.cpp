#include "../src/dllmain.cpp"
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
    std::string failure;
    int failureMode = 0, allocations = 0, modules = 0, calls = 0, hits = 0;
    uintptr_t nextHandle = 100;
    std::map<void*,std::string> live;
    std::map<void*,void*> bindings;
    std::array<unsigned char,64> mapped{};

    void Check(bool ok, const char* text) {
        if (!ok) { std::cerr << "FAIL: stage=" << failure << " mode=" << failureMode << " hits=" << hits << " calls=" << calls << ": " << text << '\n'; std::exit(1); }
        std::cout << "PASS: " << text << '\n';
    }
    std::int32_t Create(const char* stage, void** handle) {
        ++calls;
        *handle = nullptr;
        if (failure == stage) {
            ++hits;
            if (failureMode == 2) throw std::runtime_error("injected allocation exception");
            if (failureMode != 3) return failureMode == 1 ? VK_SUCCESS : -11;
        }
        *handle = reinterpret_cast<void*>(++nextHandle);
        live[*handle] = stage;
        return failure == stage ? -11 : VK_SUCCESS;
    }
    void Destroy(void*, void* handle, const void*) {
        if (!handle || live.erase(handle) != 1) { std::cerr << "double/unknown destruction\n"; std::exit(1); }
        bindings.erase(handle);
    }
    std::int32_t Bind(void*, void* object, void* memory, std::uint64_t) {
        if (failure == (live[object] == "buffer" ? "bind-buffer" : "bind-image")) { ++hits; return -11; }
        bindings[object] = memory;
        return VK_SUCCESS;
    }
    void Unreachable() { std::abort(); }
    void Setup(std::uint32_t space) {
        Check(live.empty() && bindings.empty(), "previous attempt owns no resources");
        allocations=modules=calls=hits=0;
        g_renderer={}; g_renderer.device=1; g_renderer.width=800; g_renderer.height=600;
        g_renderer.colorSpace=space;
        g_minimapFrame={}; g_minimapFrame.attempted=g_minimapFrame.loaded=true;
        g_minimapFrame.width=g_minimapFrame.height=1; g_minimapFrame.rgba={255,128,0,128};
        auto& f=g_renderer.fns;
        f.createBuffer=[](void*,const VkBufferCreateInfo*,const void*,void** h)->std::int32_t {return Create("buffer",h);};
        f.destroyBuffer=Destroy;
        f.getBufferMemoryRequirements=[](void*,void*,VkMemoryRequirements* r) {r->size=64;r->memoryTypeBits=1;};
        f.getImageMemoryRequirements=f.getBufferMemoryRequirements;
        f.createImage=[](void*,const VkImageCreateInfo*,const void*,void** h)->std::int32_t {return Create("image",h);};
        f.destroyImage=Destroy;
        f.allocateMemory=[](void*,const VkMemoryAllocateInfo*,const void*,void** h)->std::int32_t {
            return Create(++allocations==1?"staging-allocation":"image-allocation",h);
        };
        f.freeMemory=[](void* d,void* h,const void* a) {
            for(const auto& pair:bindings) if(pair.second==h) { std::cerr<<"memory freed while resource still bound\n";std::exit(1); }
            Destroy(d,h,a);
        };
        f.mapMemory=[](void*,void*,std::uint64_t,std::uint64_t,std::uint32_t,void** p)->std::int32_t {
            if(failure=="map") {++hits;*p=nullptr;return -11;}
            *p=mapped.data();return VK_SUCCESS;
        };
        f.unmapMemory=[](void*,void*) {};
        f.bindBufferMemory=Bind;f.bindImageMemory=Bind;
        f.createImageView=[](void*,const VkImageViewCreateInfo*,const void*,void** h)->std::int32_t {return Create("image-view",h);};
        f.destroyImageView=Destroy;
        f.createSampler=[](void*,const VkSamplerCreateInfo*,const void*,void** h)->std::int32_t {return Create("sampler",h);};
        f.destroySampler=Destroy;
        f.createDescriptorSetLayout=[](void*,const VkDescriptorSetLayoutCreateInfo*,const void*,void** h)->std::int32_t {return Create("descriptor-layout",h);};
        f.destroyDescriptorSetLayout=Destroy;
        f.createDescriptorPool=[](void*,const VkDescriptorPoolCreateInfo*,const void*,void** h)->std::int32_t {return Create("descriptor-pool",h);};
        f.destroyDescriptorPool=Destroy;
        f.allocateDescriptorSets=[](void*,const VkDescriptorSetAllocateInfo*,void** h)->std::int32_t {
            ++calls;*h=nullptr;
            if(failure=="descriptor-set") {++hits;return -11;}
            *h=reinterpret_cast<void*>(++nextHandle);return VK_SUCCESS;
        };
        f.updateDescriptorSets=[](void*,std::uint32_t,const VkWriteDescriptorSet*,std::uint32_t,const void*) {};
        f.createShaderModule=[](void*,const VkShaderModuleCreateInfo*,const void*,void** h)->std::int32_t {return Create(++modules==1?"vertex-module":"fragment-module",h);};
        f.destroyShaderModule=Destroy;
        f.createPipelineLayout=[](void*,const VkPipelineLayoutCreateInfo*,const void*,void** h)->std::int32_t {return Create("pipeline-layout",h);};
        f.destroyPipelineLayout=Destroy;
        f.createGraphicsPipelines=[](void*,void*,std::uint32_t,const VkGraphicsPipelineCreateInfo*,const void*,void** h)->std::int32_t {return Create("pipeline",h);};
        f.destroyPipeline=Destroy;
        f.deviceWaitIdle=[](void*)->std::int32_t {return VK_SUCCESS;};
#define UNUSED(name) f.name=reinterpret_cast<decltype(f.name)>(&Unreachable)
        UNUSED(cmdPipelineBarrier); UNUSED(cmdCopyBufferToImage); UNUSED(cmdBindPipeline);
        UNUSED(cmdBindDescriptorSets); UNUSED(cmdPushConstants); UNUSED(cmdDraw);
#undef UNUSED
        Check(f.TextureReady(),"complete frame function table");
    }
}

int main() {
    const char* stages[]={"buffer","staging-allocation","map","bind-buffer","image","image-allocation","bind-image",
        "image-view","sampler","descriptor-layout","descriptor-pool","descriptor-set","vertex-module","fragment-module","pipeline-layout","pipeline"};
    for(auto space:{0u,1000104002u,1000104008u}) {
        for(const auto* stage:stages) {
            failure=stage; failureMode=0;
            for(int attempt=0;attempt<2;++attempt) {
                Setup(space);
                Check(!TryCreateTexturedFrameResourcesLocked(g_renderer) && hits==1,"injected failure returns false at selected stage");
                Check(live.empty() && bindings.empty(),"failed frame creation releases all resources before retry");
                Check(!g_renderer.frameTextureReady && !g_renderer.frameTextureImage && !g_renderer.frameStagingBuffer && !g_renderer.framePipeline,
                    "failed frame creation publishes no ownership");
            }
        }
        for(const auto* stage:{"buffer","staging-allocation","image","image-allocation","image-view","sampler","descriptor-layout","descriptor-pool","vertex-module","fragment-module","pipeline-layout","pipeline"}) {
            failure=stage;failureMode=1;Setup(space);
            Check(!TryCreateTexturedFrameResourcesLocked(g_renderer) && hits==1 && live.empty(),"success with null handle releases previous resources");
        }
        failure="pipeline";failureMode=3;Setup(space);
        Check(!TryCreateTexturedFrameResourcesLocked(g_renderer) && hits==1 && live.empty(),"partially created failed pipeline is released");
        failure="sampler";failureMode=2;Setup(space);
        bool threw=false;
        try {TryCreateTexturedFrameResourcesLocked(g_renderer);} catch(const std::runtime_error&) {threw=true;}
        Check(threw && hits==1 && live.empty(),"exception unwinding releases uncommitted frame resources");
        failure="";failureMode=0;Setup(space);
        Check(TryCreateTexturedFrameResourcesLocked(g_renderer) && g_renderer.frameTextureReady && live.size()==10,
            "successful frame keeps resources owned by renderer and destroys temporary shader modules");
        const auto previous=calls;
        Check(TryCreateTexturedFrameResourcesLocked(g_renderer) && calls==previous,"ready frame is reused without reallocating");
        DestroyVulkanMinimapRendererLocked();
        Check(live.empty() && bindings.empty() && !g_renderer.frameTextureReady,"normal teardown releases successful frame exactly once");
    }
    Setup(0);g_renderer.fns.destroyImageView=nullptr;
    Check(!TryCreateTexturedFrameResourcesLocked(g_renderer) && live.empty(),"missing destruction function rejects allocation");
}
