#include "../src/dllmain.cpp"
#include <iostream>
static float captured[4];
static void __fastcall Capture(void*,std::uint32_t,const VkClearAttachment* a,std::uint32_t,const VkClearRect*) { std::copy(a->clearValue.color.float32,a->clearValue.color.float32+4,captured); }
static void Check(bool ok,const char* m) { if(!ok){std::cerr<<"FAIL: "<<m<<'\n';std::exit(1);} std::cout<<"PASS: "<<m<<'\n'; }
int main() {
 ModContext cfg{};
 cfg.config.GetString=[](const char*, const char* key, std::string fallback) { return !std::strcmp(key,"map_style") ? std::string("default") : fallback; };
 RefreshMinimapConfig(&cfg);
 Check(g_defaultMapStyle,"default selects the new map");
 VkSwapchainCreateInfoKHR info{}; info.imageColorSpace=1000104002;
 RememberSwapchainCreate(reinterpret_cast<void*>(1),&info,reinterpret_cast<void*>(2),0);
 Check(g_swapchains.back().colorSpace==info.imageColorSpace,"capture colour space");
 VulkanMinimapRenderer r{}; r.width=800;r.height=600;r.fns.cmdClearAttachments=Capture;
 std::vector<VkClearRect> rects(1);
 CmdClearRects(r,nullptr,.2f,.4f,.6f,.5f,rects);
 Check(captured[0]==.2f&&captured[1]==.4f&&captured[2]==.6f&&captured[3]==.5f,"SDR and alpha unchanged");
 r.colorSpace=1000104002;
 CmdClearRects(r,nullptr,1,1,1,.5f,rects);
 Check(std::abs(captured[0]-2.5375f)<.0001f&&captured[0]==captured[1]&&captured[1]==captured[2]&&captured[3]==.5f,"203-nit scRGB white");
 CmdClearRect(r,nullptr,.5f,.5f,.5f,1,0,0,10,10);
 Check(std::abs(captured[0]-.5431294f)<.0001f,"single rectangle uses same HDR transform");
 r.colorSpace=1000104008;
 CmdClearRects(r,nullptr,1,1,1,1,rects);
 Check(std::abs(captured[0]-.5806889f)<.0001f&&std::abs(captured[0]-captured[1])<.0001f&&std::abs(captured[1]-captured[2])<.0001f,"PQ white stays neutral");
 CmdClearRects(r,nullptr,1,0,0,1,rects);
 Check(captured[0]>captured[1]&&captured[1]>captured[2]&&captured[2]>0,"Rec2020 red conversion");
 CmdClearRects(r,nullptr,0,0,0,0,rects);
 Check(captured[0]<.00001f&&captured[1]<.00001f&&captured[2]<.00001f&&captured[3]==0,"black and transparency preserved");
 SwapchainRuntimeInfo snap{}; snap.width=r.width;snap.height=r.height;r.ready=true;
 Check(!RendererMatchesSwapchain(r,snap,r.width,r.height,r.format),"colour-space change invalidates cached renderer");
 std::vector<std::string> candidates;AddRealMapCandidates(candidates,"test");
 Check(candidates.front().find("2048")!=std::string::npos,"2048 assets accepted before lower resolution");
 int dimension=0;Check(TryResolveRgbaMapDimensions(2048LL*2048*4,dimension)&&dimension==2048,"2048 raw map accepted");
 Check(!TryResolveRgbaMapDimensions(2048LL*2048*4-1,dimension),"truncated map rejected");
 for(unsigned space : {1000104002u,1000104008u}) for(int v=0;v<256;++v) {
  float a=v/255.f,b=a,c=a;EncodeOverlayColor(space,a,b,c);
  Check(std::isfinite(a)&&std::abs(a-b)<.0001f&&std::abs(b-c)<.0001f,"neutral ramp finite and neutral");
 }
}
