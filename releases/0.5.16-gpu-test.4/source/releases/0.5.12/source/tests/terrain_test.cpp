#define main FrameResourceChecks
#include "frame_failure_test.cpp"
#undef main

int terrainDraws = 0, terrainCopies = 0, terrainClears = 0;
bool inRenderPass = false;
std::vector<std::string> rendererDiagnosticMessages;
VkBufferImageCopy lastCopy{};
float terrainPush[32]{};

void RunCpuOptimizationChecks() {
const bool realMapLoadedBeforeBatch = g_realMap.loaded;
    g_renderer.terrainPrepared=false; g_realMap.loaded=false;
    const int repeatedColorCallsBefore=terrainClears;
    DrawRealMap(g_renderer,nullptr,160,120,17,5120,5120,3,0,{false,nullptr});
    const int repeatedColorCalls=terrainClears-repeatedColorCallsBefore;
    g_realMap.loaded=realMapLoadedBeforeBatch;
    Check(repeatedColorCalls==1,"repeated-color CPU terrain batches its row rectangles into one clear command");
    Check(g_renderer.terrainClearBatches.size()<=16 && !g_renderer.terrainClearBatches.empty() &&
        g_renderer.terrainClearBatches[0].rects.size()==512,
        "CPU terrain clear scratch storage remains bounded");
    const auto* batchStorage=g_renderer.terrainClearBatches[0].rects.data();
    g_renderer.terrainPrepared=false;
    DrawRealMap(g_renderer,nullptr,160,120,17,5120,5120,3,0,{false,nullptr});
    Check(g_renderer.terrainClearBatches[0].rects.data()==batchStorage,
        "repeated CPU terrain recording reuses its clear-rectangle storage");
    const int priorScale=g_minimapScalePercent.load();
    g_minimapScalePercent=75;
    g_renderer.clearAttachmentCommands=999; g_renderer.clearAttachmentRects=999;
    g_renderer.terrainUploadRegions=7; g_renderer.terrainUploadBytes=8192;
    const bool priorTerrainDisabled = g_renderer.terrainDisabled;
    g_renderer.terrainDisabled = true;
    Check(RecordVulkanMinimapCommandLocked(0) && g_renderer.terrainDrawAttempted && !g_renderer.terrainGpuDrawn,
        "CPU terrain recording succeeds and reports the actual fallback draw path");
    Check(g_renderer.clearAttachmentCommands>0 && g_renderer.clearAttachmentRects>0 &&
        g_renderer.terrainUploadRegions==0 && g_renderer.terrainUploadBytes==0 &&
        g_renderer.recordingCpuNanoseconds>0 &&
        std::strcmp(g_renderer.terrainFallbackReason,"resources-disabled")==0,
        "recording resets command and upload counters and measures CPU time only");
    g_renderer.terrainDisabled = priorTerrainDisabled;
    const auto ordinaryClear=g_renderer.fns.cmdClearAttachments;
    static std::vector<std::uint8_t> coverage(320*240);
    g_renderer.fns.cmdClearAttachments=[](void*,std::uint32_t,const VkClearAttachment*,std::uint32_t count,const VkClearRect* rects) {
        for(std::uint32_t i=0;i<count;++i)
            for(unsigned y=0;y<rects[i].rect.extent.height;++y)
                for(unsigned x=0;x<rects[i].rect.extent.width;++x)
                    coverage[(rects[i].rect.offset.y+y)*320+rects[i].rect.offset.x+x]=1;
    };
    g_renderer.width=320;g_renderer.height=240;g_minimapMapSampleStep=3;
    for(int percent:{75,125,175}) {
        const float scale=percent/100.f;
        g_minimapScalePercent=100; // Exercise the effective widget scale independently of the saved setting.
        std::fill(coverage.begin(),coverage.end(),0);
        std::vector<std::uint8_t> expected(coverage.size());
        const auto drawsBefore=terrainDraws,copiesBefore=terrainCopies,callsBefore=calls;
        const bool priorScaleTerrainDisabled = g_renderer.terrainDisabled;
        g_renderer.terrainDisabled = true;
        {
            MinimapScaleScope scope(g_renderer,scale,160,120);
            Check(!PrepareTerrainTexture(g_renderer,0,nullptr,{false,nullptr}),"forced resource disable selects the CPU fallback at the effective widget size");
            DrawRealMap(g_renderer,nullptr,160,120,17,5120,5120,3,0,{false,nullptr});
        }
        g_renderer.terrainDisabled = priorScaleTerrainDisabled;
        for(int y=-17;y<=17;y+=3) for(int x=-17;x<=17;x+=3) {
            const int w=(std::min)(3,18-x),h=(std::min)(3,18-y),sx=x+w/2,sy=y+h/2;
            if(sx*sx+sy*sy>17*17)continue;
            const int left=std::lround(160+x*scale),right=std::lround(160+(x+w)*scale);
            const int top=std::lround(120+y*scale),bottom=std::lround(120+(y+h)*scale);
            for(int yy=top;yy<bottom;++yy)for(int xx=left;xx<right;++xx)expected[yy*320+xx]=1;
        }
        Check(coverage==expected && terrainDraws==drawsBefore && terrainCopies==copiesBefore && calls==callsBefore,
            "75/125/175 percent terrain uses the exact legacy rounded cell footprint without GPU draws or uploads");
    }
    g_renderer.fns.cmdClearAttachments=ordinaryClear;g_renderer.width=800;g_renderer.height=600;
    g_minimapScalePercent=100;g_minimapMapSampleStep=MINIMAP_DEFAULT_MAP_SAMPLE_STEP;
    g_minimapScalePercent=priorScale;
    ModContext diagnosticContext{};
    diagnosticContext.Log=[](const char* message){rendererDiagnosticMessages.emplace_back(message);};
    g_modContext=&diagnosticContext; g_debugLoggingEnabled=true;
    VulkanMinimapRenderer diagnosticRenderer{};
    diagnosticRenderer.terrainDrawAttempted=true;
    diagnosticRenderer.terrainFallbackReason="gpu_terrain_disabled";
    diagnosticRenderer.clearAttachmentCommands=3;
    diagnosticRenderer.clearAttachmentRects=17;
    diagnosticRenderer.terrainUploadRegions=2;
    diagnosticRenderer.terrainUploadBytes=4096;
    diagnosticRenderer.recordingCpuNanoseconds=123456;
    Check(LogRendererDiagnostics(diagnosticRenderer,100) && rendererDiagnosticMessages.size()==1,
        "renderer diagnostic logs CPU fallback path and counters");
    Check(rendererDiagnosticMessages.back().find("terrain=cpu-fallback")!=std::string::npos &&
        rendererDiagnosticMessages.back().find("fallback=gpu_terrain_disabled")!=std::string::npos &&
        rendererDiagnosticMessages.back().find("clear_commands=3")!=std::string::npos &&
        rendererDiagnosticMessages.back().find("clear_rects=17")!=std::string::npos &&
        rendererDiagnosticMessages.back().find("upload_regions=2")!=std::string::npos &&
        rendererDiagnosticMessages.back().find("upload_bytes=4096")!=std::string::npos &&
        rendererDiagnosticMessages.back().find("gpu_time=unavailable")!=std::string::npos,
        "renderer diagnostic names fallback reason and never implies GPU timing");
    Check(!LogRendererDiagnostics(diagnosticRenderer,5099) && rendererDiagnosticMessages.size()==1,
        "renderer diagnostics are rate-limited before another message is built");
    diagnosticRenderer.terrainGpuDrawn=true; diagnosticRenderer.terrainFallbackReason=nullptr;
    Check(LogRendererDiagnostics(diagnosticRenderer,5100) &&
        rendererDiagnosticMessages.back().find("terrain=gpu")!=std::string::npos &&
        rendererDiagnosticMessages.back().find("fallback=none")!=std::string::npos,
        "renderer diagnostic reports a successful GPU terrain draw");
    diagnosticRenderer.terrainDrawAttempted=false; diagnosticRenderer.terrainGpuDrawn=false;
    Check(LogRendererDiagnostics(diagnosticRenderer,10100) &&
        rendererDiagnosticMessages.back().find("terrain=not-drawn")!=std::string::npos,
        "renderer diagnostic distinguishes hidden or skipped terrain from CPU fallback");
    g_debugLoggingEnabled=false; g_modContext=nullptr;
}
int main(int argc, char** argv) {
    failure=""; failureMode=0; Setup(0);
    mapped.resize(64*1024*1024);
    g_renderer.fns.flushMappedMemoryRanges=[](void*,std::uint32_t,const VkMappedMemoryRange*)->std::int32_t{return 0;};
    g_renderer.commandBuffers={1}; g_renderer.framebuffers={2}; g_renderer.ready=true;
    g_renderer.fns.resetCommandBuffer=[](void*,std::uint32_t)->std::int32_t{return 0;};
    g_renderer.fns.beginCommandBuffer=[](void*,const VkCommandBufferBeginInfo*)->std::int32_t{return 0;};
    g_renderer.fns.endCommandBuffer=[](void*)->std::int32_t{return 0;};
    g_renderer.fns.cmdBeginRenderPass=[](void*,const VkRenderPassBeginInfo*,std::uint32_t){inRenderPass=true;};
    g_renderer.fns.cmdEndRenderPass=[](void*){inRenderPass=false;};
    g_renderer.fns.cmdClearAttachments=[](void*,std::uint32_t,const VkClearAttachment*,std::uint32_t,const VkClearRect*){++terrainClears;};
    g_renderer.fns.cmdPipelineBarrier=[](void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,const void*,std::uint32_t,const void*,std::uint32_t,const VkImageMemoryBarrier*){};
    g_renderer.fns.cmdCopyBufferToImage=[](void*,void*,void*,std::uint32_t,std::uint32_t n,const VkBufferImageCopy* copies){
        Check(!inRenderPass && n==1,"texture transfers remain outside the render pass");
        lastCopy=*copies;++terrainCopies;
    };
    g_renderer.fns.cmdBindPipeline=[](void*,std::uint32_t,void*){};
    g_renderer.fns.cmdBindDescriptorSets=[](void*,std::uint32_t,void*,std::uint32_t,std::uint32_t,void* const*,std::uint32_t,const std::uint32_t*){};
    g_renderer.fns.cmdPushConstants=[](void*,void*,std::uint32_t stages,std::uint32_t,std::uint32_t bytes,const void* values){
        Check(stages==(VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT) && bytes==sizeof(terrainPush),"terrain push constants fit the Vulkan minimum range");
        std::memcpy(terrainPush,values,bytes);
    };
    g_renderer.fns.cmdDraw=[](void*,std::uint32_t n,std::uint32_t,std::uint32_t,std::uint32_t){Check(n==6,"terrain quad has six vertices");++terrainDraws;};
    g_minimapFrame.loaded=false;
    g_realMap.attempted=g_realMap.loaded=true; g_realMap.width=g_realMap.height=512;
    g_realMap.rgba.assign(512*512*4,128); g_defaultMapStyle=false;
    g_staticPois.attempted=true; g_minimapVisible=true;
    g_playerPosition.valid=true;g_playerPosition.lastUpdateTick=GetTickCount();
    g_playerPosition.x=g_playerPosition.z=std::int64_t(5120)*4294967296LL;
    RunCpuOptimizationChecks();
    if (argc > 1 && std::strcmp(argv[1], "--cpu-only") == 0) return 0;
    for(int percent:{50,75,100,125,150,175,200}) {
        g_minimapScalePercent=percent;
        const auto previousDraws=terrainDraws;
        Check(RecordVulkanMinimapCommandLocked(0),"all-size terrain command records");
        Check(terrainDraws==previousDraws+1 && g_renderer.terrainPrepared,
            "all seven sizes prepare and select the GPU route before entering widget scope");
        if(percent==50)
            Check(g_renderer.terrainUploadBytes==512ULL*513*4 && g_renderer.terrainUploadRegions==1,
                "first terrain recording counts full atlas initialization bytes and region");
        else
            Check(g_renderer.terrainUploadBytes==0 && g_renderer.terrainUploadRegions==0,
                "subsequent terrain size recordings reset upload counters");
    }
    terrainDraws=0;
    g_minimapScalePercent=100;
    Check(RecordVulkanMinimapCommandLocked(0),"terrain command records");
    Check(terrainDraws==1,"terrain uses one GPU draw instead of CPU colour runs");
    Check(terrainCopies==1,"all sizes share the slot atlas initialized once");
    Check(g_renderer.terrainUploadBytes==0 && g_renderer.terrainUploadRegions==0,
        "repeated terrain recording resets upload counters to zero");
    const auto ordinaryClear=g_renderer.fns.cmdClearAttachments;
    static std::vector<std::uint8_t> coverage(320*240);
    g_renderer.fns.cmdClearAttachments=[](void*,std::uint32_t,const VkClearAttachment*,std::uint32_t count,const VkClearRect* rects) {
        for(std::uint32_t i=0;i<count;++i)
            for(unsigned y=0;y<rects[i].rect.extent.height;++y)
                for(unsigned x=0;x<rects[i].rect.extent.width;++x)
                    coverage[(rects[i].rect.offset.y+y)*320+rects[i].rect.offset.x+x]=1;
    };
    g_renderer.width=320;g_renderer.height=240;g_minimapMapSampleStep=3;
    for(int percent:{75,125,175}) {
        const float scale=percent/100.f;
        g_minimapScalePercent=100; // Exercise the effective widget scale independently of the saved setting.
        std::fill(coverage.begin(),coverage.end(),0);
        std::vector<std::uint8_t> expected(coverage.size());
        const auto drawsBefore=terrainDraws,copiesBefore=terrainCopies,callsBefore=calls;
        {
            MinimapScaleScope scope(g_renderer,scale,160,120);
            Check(PrepareTerrainTexture(g_renderer,0,nullptr,{false,nullptr}),"effective resized scope prepares GPU terrain independently of the saved setting");
            g_renderer.terrainPrepared=false; // Exercise the retained error fallback's original rasterization.
            DrawRealMap(g_renderer,nullptr,160,120,17,5120,5120,3,0,{false,nullptr});
        }
        g_renderer.terrainPrepared=true;
        for(int y=-17;y<=17;y+=3) for(int x=-17;x<=17;x+=3) {
            const int w=(std::min)(3,18-x),h=(std::min)(3,18-y),sx=x+w/2,sy=y+h/2;
            if(sx*sx+sy*sy>17*17)continue;
            const int left=std::lround(160+x*scale),right=std::lround(160+(x+w)*scale);
            const int top=std::lround(120+y*scale),bottom=std::lround(120+(y+h)*scale);
            for(int yy=top;yy<bottom;++yy)for(int xx=left;xx<right;++xx)expected[yy*320+xx]=1;
        }
        Check(coverage==expected && terrainDraws==drawsBefore && terrainCopies==copiesBefore && calls==callsBefore,
            "error fallback preserves the exact legacy rounded cell footprint without GPU draws or uploads");
    }
    g_renderer.fns.cmdClearAttachments=ordinaryClear;g_renderer.width=800;g_renderer.height=600;
    g_minimapScalePercent=100;g_minimapMapSampleStep=MINIMAP_DEFAULT_MAP_SAMPLE_STEP;
    const int allocationsBefore=calls;
    for(int i=0;i<100;++i) {
        g_playerPosition.x+=4294967296LL;
        Check(RecordVulkanMinimapCommandLocked(0),"moving terrain records");
    }
    Check(terrainDraws==101 && terrainCopies==1 && calls==allocationsBefore,"movement uses one draw with no immutable map upload or Vulkan allocation");
    const auto directDraws=terrainDraws,directClears=terrainClears,directCopies=terrainCopies;
    DrawRealMap(g_renderer,nullptr,160,120,17,5120,5120,3,0,{false,nullptr});
    Check(terrainDraws==directDraws+1 && terrainClears==directClears && terrainCopies==directCopies,
        "100 percent terrain keeps one GPU draw and zero CPU terrain clears");
    g_liveFogLayoutSupported=true;g_showFogOfWar=true;
    auto fog=std::make_shared<LiveFogSnapshot>();fog->columns=fog->rows=1;
    fog->worldWidth=fog->worldHeight=256;fog->session=g_worldSessionGeneration;fog->tick=GetTickCount();fog->tiles.assign(1024,255);
    g_liveFog=fog;
    Check(RecordVulkanMinimapCommandLocked(0),"fog dimensions rebuild only the reusable image slot");
    const int withFog=calls, copiesWithFog=terrainCopies;
    Check(terrainPush[20]==1 && terrainPush[21]==1,"live fog is enabled in the terrain draw");
    Check(RecordVulkanMinimapCommandLocked(0) && calls==withFog && terrainCopies==copiesWithFog,"unchanged fog snapshot needs no upload");
    fog=std::make_shared<LiveFogSnapshot>(*fog);fog->tiles[0]=0;g_liveFog=fog;
    g_renderer.terrainUploadBytes=0;g_renderer.terrainUploadRegions=0;
    Check(RecordVulkanMinimapCommandLocked(0) && calls==withFog && terrainCopies==copiesWithFog+1,"changed exploration uploads fog without reallocating or uploading the map");
    Check(g_renderer.terrainUploadBytes==4096 && g_renderer.terrainUploadRegions==1,
        "terrain diagnostics count actual dirty tile bytes and regions");
    Check(lastCopy.imageOffset.y==512 && lastCopy.imageExtent.width==32 && lastCopy.imageExtent.height==32 && lastCopy.bufferOffset==512ULL*512*4,"partial upload contains only one dirty fog tile");
    const auto readyFog=g_renderer.terrainSlots[0].fogSnapshot;
    auto failedFog=std::make_shared<LiveFogSnapshot>(*fog);failedFog->tiles[0]=17;failedFog->tick=GetTickCount();
    const auto goodMap=g_renderer.fns.mapMemory;
    g_renderer.fns.mapMemory=[](void*,void*,std::uint64_t,std::uint64_t,std::uint32_t,void**)->std::int32_t{return -11;};
    const auto beforeFailedCopy=terrainCopies;
    Check(!PrepareTerrainTexture(g_renderer,0,nullptr,{true,failedFog}) && g_renderer.terrainSlots[0].fogSnapshot==readyFog && terrainCopies==beforeFailedCopy,
        "failed dirty map keeps the accepted slot snapshot and issues no upload");
    Check(!TryDrawTerrainTexture(g_renderer,nullptr,100,100,31,5120,5120,7,0,{true,failedFog}),
        "failed dirty refresh cannot draw a mismatched exploration mask");
    g_renderer.fns.mapMemory=goodMap;
    const auto goodFlush=g_renderer.fns.flushMappedMemoryRanges;
    g_renderer.fns.flushMappedMemoryRanges=[](void*,std::uint32_t,const VkMappedMemoryRange*)->std::int32_t{return -11;};
    Check(!PrepareTerrainTexture(g_renderer,0,nullptr,{true,failedFog}) && g_renderer.terrainSlots[0].fogSnapshot==readyFog && terrainCopies==beforeFailedCopy,
        "failed dirty flush does not publish readiness or upload");
    g_renderer.fns.flushMappedMemoryRanges=goodFlush;
    Check(PrepareTerrainTexture(g_renderer,0,nullptr,{true,failedFog}) && g_renderer.terrainSlots[0].fogSnapshot==failedFog && terrainCopies==beforeFailedCopy+1,
        "retry after failed flush safely uploads the tile from the accepted snapshot");
    auto sameFog=std::make_shared<LiveFogSnapshot>(*failedFog);sameFog->tick=GetTickCount();
    g_renderer.fns.mapMemory=[](void*,void*,std::uint64_t,std::uint64_t,std::uint32_t,void**)->std::int32_t{return -11;};
    Check(PrepareTerrainTexture(g_renderer,0,nullptr,{true,sameFog}) && g_renderer.terrainSlots[0].fogSnapshot==sameFog && terrainCopies==beforeFailedCopy+1,
        "identical changed snapshot bypasses conversion, mapping, flushing and copying");
    g_renderer.fns.mapMemory=goodMap;
    fog->tick=GetTickCount()-2001;
    Check(RecordVulkanMinimapCommandLocked(0) && terrainPush[20]==1 && terrainPush[21]==0,"expired exploration draws opaque fog without reusing the old mask");
    ++g_worldSessionGeneration;fog->tick=GetTickCount();
    Check(!IsTerrainFogValid({true,fog}),"snapshot from a previous session is rejected even after frame capture");
    Check(RecordVulkanMinimapCommandLocked(0) && terrainPush[21]==0,"session change prevents exploration from the previous world");
    auto newSessionFog=std::make_shared<LiveFogSnapshot>(*sameFog);
    newSessionFog->session=g_worldSessionGeneration;newSessionFog->tick=GetTickCount();g_liveFog=newSessionFog;
    const auto sessionImage=g_renderer.terrainSlots[0].frameTextureImage;
    g_renderer.terrainUploadBytes=0;g_renderer.terrainUploadRegions=0;
    Check(RecordVulkanMinimapCommandLocked(0) && terrainPush[21]==1 &&
        g_renderer.terrainSlots[0].frameTextureImage!=sessionImage &&
        g_renderer.terrainUploadBytes==512ULL*544*4,
        "new world session initializes the complete per-slot atlas before publishing its fog");
    auto newWorldFog=std::make_shared<LiveFogSnapshot>(*newSessionFog);
    newWorldFog->worldWidth=512;newWorldFog->tick=GetTickCount();g_liveFog=newWorldFog;
    const auto worldImage=g_renderer.terrainSlots[0].frameTextureImage;
    Check(RecordVulkanMinimapCommandLocked(0) && g_renderer.terrainSlots[0].frameTextureImage!=worldImage && terrainPush[18]==512,
        "changed world dimensions reinitialize rather than reusing previous-world fog");
    auto largerFog=std::make_shared<LiveFogSnapshot>(*newWorldFog);
    largerFog->columns=largerFog->rows=2;largerFog->tiles.assign(4096,255);largerFog->tick=GetTickCount();g_liveFog=largerFog;
    Check(RecordVulkanMinimapCommandLocked(0) && terrainPush[16]==64 && terrainPush[17]==64,
        "changed fog dimensions initialize the resized atlas");
    auto multiFog=std::make_shared<LiveFogSnapshot>(*largerFog);
    multiFog->tiles[0]=42;multiFog->tiles[3*1024]=17;multiFog->tick=GetTickCount();
    g_renderer.terrainUploadBytes=0;g_renderer.terrainUploadRegions=0;
    // Real conversion uses the fake mapped allocation, whose untouched bytes expose excess writes.
    std::fill(mapped.begin()+512*512*4,mapped.begin()+576*512*4,91);
    const auto oneRegionCopy=g_renderer.fns.cmdCopyBufferToImage;
    g_renderer.fns.cmdCopyBufferToImage=[](void*,void*,void*,std::uint32_t,std::uint32_t count,const VkBufferImageCopy* copies){
        Check(!inRenderPass && count==2,"two separated dirty tiles remain two bounded copy regions");
        Check(copies[0].imageOffset.x==0 && copies[0].imageOffset.y==512 &&
            copies[1].imageOffset.x==32 && copies[1].imageOffset.y==544,
            "dirty copies address independent tile positions in the atlas");
        ++terrainCopies;
    };
    Check(PrepareTerrainTexture(g_renderer,0,nullptr,{true,multiFog}) && g_renderer.terrainUploadBytes==8192 &&
        g_renderer.terrainUploadRegions==2,"two separated dirty tiles upload exactly 8192 bytes");
    bool conversionLocal=true;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){
        const int tile=(y/32)*2+x/32;const auto offset=(std::size_t(512+y)*512+x)*4;
        const auto expected=tile==0||tile==3?multiFog->tiles[tile*1024+(y%32)*32+x%32]:91;
        conversionLocal &= mapped[offset]==expected && mapped[offset+1]==91 && mapped[offset+2]==91 && mapped[offset+3]==91;
    }
    Check(conversionLocal,"dirty conversion rewrites only red bytes in changed tiles and leaves other pixels untouched");
    g_renderer.fns.cmdCopyBufferToImage=oneRegionCopy;
    g_showFogOfWar=false;
    Check(RecordVulkanMinimapCommandLocked(0) && terrainPush[20]==0,"fog visibility setting updates without a texture rebuild");
    g_defaultMap.attempted=g_defaultMap.loaded=true;g_defaultMap.width=g_defaultMap.height=1024;
    g_defaultMap.rgba.assign(1024*1024*4,255);g_defaultMapStyle=true;
    Check(RecordVulkanMinimapCommandLocked(0) && terrainPush[12]==1024,"HD selection switches the terrain texture");
    Check(live.size()==10,"replacing a slot destroys its previous resources");
    const auto slotZeroImage=g_renderer.terrainSlots[0].frameTextureImage;
    g_renderer.commandBuffers.push_back(3);g_renderer.framebuffers.push_back(4);
    Check(RecordVulkanMinimapCommandLocked(1) && live.size()==20 && g_renderer.terrainSlots[0].frameTextureImage==slotZeroImage,"second swapchain image owns independent staging and descriptors");
    g_defaultMapStyle=false;
    Check(RecordVulkanMinimapCommandLocked(1) && terrainPush[12]==512 && live.size()==20,"Original switch replaces only the completed image slot");
    const auto pendingCalls=calls;
    g_renderer.submissionFences={7,8};g_renderer.renderCompleteSemaphores={9,10};
    g_renderer.fns.getFenceStatus=[](void*,void*)->std::int32_t{return VK_NOT_READY;};
    VkPresentInfoKHR present{},adjusted{};const void* semaphore=nullptr;
    Check(!SubmitVulkanMinimapFrameLocked(reinterpret_cast<void*>(1),present,0,adjusted,&semaphore) && calls==pendingCalls,"busy image fence skips texture mutation and recording");
    DestroyVulkanMinimapRendererLocked();
    Check(live.empty() && bindings.empty(),"renderer recreation releases every terrain slot exactly once");
    for(const auto* stage:{"buffer","staging-allocation","map","bind-buffer","image","image-allocation","bind-image","image-view","sampler","descriptor-layout","descriptor-pool","descriptor-set","vertex-module","fragment-module","pipeline-layout","pipeline","flush"}) {
        failure=stage;failureMode=0;Setup(0);
        g_renderer.fns.flushMappedMemoryRanges=[](void*,std::uint32_t,const VkMappedMemoryRange*)->std::int32_t {
            if(failure=="flush"){++hits;return -11;}return 0;
        };
        TexturedResources resource;
        Check(!TryCreateTexturedResourcesLocked(g_renderer,resource,std::vector<std::uint8_t>(16,255),2,2,true) && hits==1 && live.empty() && bindings.empty(),"optional terrain allocation failure releases all partial ownership");
        Check(!resource.frameTextureReady,"failed optional terrain cannot be drawn");
    }
    failure="";Setup(0);
    g_renderer.fns.flushMappedMemoryRanges=[](void*,std::uint32_t,const VkMappedMemoryRange*)->std::int32_t{return 0;};
    TexturedResources invalid;
    Check(!TryCreateTexturedResourcesLocked(g_renderer,invalid,std::vector<std::uint8_t>(16),0,2,true) && calls==0,
        "invalid texture dimensions are rejected before Vulkan allocation");
    return 0;
}
