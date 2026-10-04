// Production readers and drawing commands, without hooking or modifying a game.
#include "../src/dllmain.cpp"
#include <iostream>

namespace
{
    void Check(bool ok, const char* message)
    {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
        std::cout << "PASS: " << message << '\n';
    }
    template<class T> void Put(void* data, std::size_t offset, T value)
    { std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value)); }
    std::int64_t Fixed(int value) { return static_cast<std::int64_t>(value) * 4294967296LL; }
    std::vector<std::uint8_t> state(DEATH_MAP_ARRAY_OFFSET + 32);
    std::array<std::uint8_t, WORLD_MAP_MARKER_STRIDE * 5> world{};
    std::array<std::uint8_t, WORLD_MAP_MARKER_STRIDE> custom{};
    void Entry(void* data, std::size_t index, int x, int z, std::uint32_t key)
    {
        const auto offset = index * WORLD_MAP_MARKER_STRIDE;
        Put(data, offset + 0x10, Fixed(x)); Put(data, offset + 0x18, Fixed(200));
        Put(data, offset + 0x20, Fixed(z)); Put(data, offset + 0x30, key);
    }
    void __fastcall InitMap(void* ctx, void* record, std::uint32_t size)
    {
        if (size != 16) std::exit(2);
        static_cast<uintptr_t*>(ctx)[1] = 999;
        static_cast<uintptr_t*>(record)[1] = reinterpret_cast<uintptr_t>(state.data());
    }
    struct Pixel { float r = 0, g = 0, b = 0; } pixels[320][512];
    int clears = 0;
    void Raster(void*, std::uint32_t, const VkClearAttachment* a, std::uint32_t count, const VkClearRect* rects)
    {
        ++clears;
        for (std::uint32_t i = 0; i < count; ++i)
            for (std::uint32_t dy = 0; dy < rects[i].rect.extent.height; ++dy)
                for (std::uint32_t dx = 0; dx < rects[i].rect.extent.width; ++dx)
                {
                    int x = rects[i].rect.offset.x + dx, y = rects[i].rect.offset.y + dy;
                    if (x < 0 || x >= 512 || y < 0 || y >= 320) std::exit(3);
                    pixels[y][x] = { a->clearValue.color.float32[0], a->clearValue.color.float32[1], a->clearValue.color.float32[2] };
                }
    }
    void WritePreview(const char* path)
    {
        BITMAPFILEHEADER h{}; BITMAPINFOHEADER info{};
        h.bfType = 0x4D42; h.bfOffBits = sizeof(h) + sizeof(info); h.bfSize = h.bfOffBits + sizeof(pixels) / sizeof(Pixel) * 4;
        info.biSize = sizeof(info); info.biWidth = 512; info.biHeight = -320;
        info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h)); out.write(reinterpret_cast<const char*>(&info), sizeof(info));
        for (const auto& row : pixels) for (const auto& p : row)
        {
            const std::uint8_t color[] = { static_cast<std::uint8_t>(p.b * 255), static_cast<std::uint8_t>(p.g * 255), static_cast<std::uint8_t>(p.r * 255), 255 };
            out.write(reinterpret_cast<const char*>(color), 4);
        }
        Check(out.good(), "CPU preview written");
    }
    void CheckDeaths(VulkanMinimapRenderer& renderer)
    {
        std::array<std::uint8_t, WORLD_MAP_MARKER_STRIDE * 3> deaths{};
        const uintptr_t ui = reinterpret_cast<uintptr_t>(state.data());
        const DWORD now = GetTickCount();
        Entry(deaths.data(), 0, 1020, 1000, 0x1AD8F96E);
        Entry(deaths.data(), 1, 1000, 1040, 0x1AD8F96E);
        Entry(deaths.data(), 2, -10000, 1000, 0x1AD8F96E);
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(deaths.data()));
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{3});
        g_deathMarkerLayoutSupported = g_worldMapLayoutSupported = true;
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).size() == 2, "multiple tombstones are captured; invalid coordinates are omitted");
        Check(CopyWorldMapMarkers(now).size() == 1, "death capture preserves existing POIs");
        Check(CopyDeathMarkers(now + 2001).empty(), "interrupted tombstone feed expires");
        CaptureWorldMapMarkers(ui, 0xFFFFFFF0u);
        Check(CopyDeathMarkers(0x10).size() == 2, "tombstone freshness survives tick wrap");
        Entry(deaths.data(), 0, 1050, 1000, 0x1AD8F96E);
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now)[0].x == Fixed(1050), "tombstone positions update from each completed snapshot");
        g_showDeathMarkers = false;
        Check(CopyDeathMarkers(now).empty(), "death visibility toggle hides existing tombstones");
        g_showDeathMarkers = true;
        Check(CopyDeathMarkers(now).size() == 2, "death visibility toggle restores the current snapshot");

        // Exercise the real draw path: ordinary POI limits and visibility cannot
        // suppress deaths; even coincident tombstones remain separate records.
        Entry(deaths.data(), 1, 1050, 1000, 0x1AD8F96E);
        CaptureWorldMapMarkers(ui, GetTickCount());
        g_showWorldMarkers = false; g_minimapMaxDrawnPoints = 8;
        std::memset(pixels, 0, sizeof(pixels)); clears = 0;
        DrawLiveMarkers(renderer, nullptr, {}, 1000, 1000, 1, 0, 160, 160, 130);
        Check(clears == 4 && pixels[155][210].r > 0.9f, "death layer renders skulls independently of POI visibility, limits and deduplication");
        Check(pixels[159][207].r < 0.1f, "skull fallback keeps dark eye sockets");
        g_showDeathMarkers = false; clears = 0;
        DrawLiveMarkers(renderer, nullptr, {}, 1000, 1000, 1, 0, 160, 160, 130);
        Check(clears == 0, "disabled death layer issues no draw commands");
        g_showDeathMarkers = true; g_showWorldMarkers = true;
        g_minimapMaxDrawnPoints = MINIMAP_DEFAULT_MAX_DRAWN_POINTS;
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
        Entry(deaths.data(), 0, 9000, 1000, 0x1AD8F96E);
        CaptureWorldMapMarkers(ui, GetTickCount());
        for (float heading : {0.0f, 1.5707963f, 3.1415926f, 4.7123889f})
        {
            for (float zoom : {0.5f, 12.0f})
            {
                std::memset(pixels, 0, sizeof(pixels)); clears = 0;
                DrawLiveMarkers(renderer, nullptr, {}, 1000, 1000, zoom, heading, 160, 160, 130);
                bool outside = false;
                for (int y=0; y<320; ++y) for (int x=0; x<512; ++x)
                    if ((x-160)*(x-160)+(y-160)*(y-160)>130*130 && pixels[y][x].r != 0) outside=true;
                Check(clears == 2 && !outside, "distant skull remains inside the rim through rotation and zoom");
            }
        }
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty(), "recovering the final tombstone removes its marker");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).size() == 1, "a later death creates a fresh marker");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{DEATH_MAP_MAX_ENTRIES + 1});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty() && CopyWorldMapMarkers(now).size() == 1,
            "oversized death array clears only deaths and preserves POIs");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET, uintptr_t{1});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty() && CopyWorldMapMarkers(now).size() == 1,
            "unreadable death array clears only deaths and preserves POIs");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(deaths.data()));
        CaptureWorldMapMarkers(ui, now);
        ClearLiveMarkers();
        Check(CopyDeathMarkers(now).empty(), "world exit clears tombstones");
        g_deathMarkerLayoutSupported = false;
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty() && CopyWorldMapMarkers(now).size() == 1,
            "unsupported death layout leaves existing POI capture working");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
        ClearLiveMarkers();
        g_deathMarkerLayoutSupported = true;
    }

    std::array<std::uint8_t, 0x70> daytimeState{};
    void __fastcall InitDaytime(void* ctx, void* record, std::uint32_t size)
    {
        if (size != 0x28) std::exit(2);
        static_cast<uintptr_t*>(ctx)[1] = 999;
        static_cast<uintptr_t*>(record)[2] = reinterpret_cast<uintptr_t>(daytimeState.data());
    }
    void CheckWorldClock(VulkanMinimapRenderer& renderer)
    {
        const auto ns = CLOCK_NANOSECONDS_PER_MINUTE;
        const auto ptr = reinterpret_cast<uintptr_t>(daytimeState.data());
        Put(daytimeState.data(), 0x28, 360 * ns);
        Put(daytimeState.data(), 0x30, 1080 * ns);
        WorldClockSnapshot clock{};
        for (int minute : {0, 359, 360, 719, 720, 1079, 1080, 1439})
        {
            Put(daytimeState.data(), 0x48, minute * ns + ns - 1);
            Check(ReadWorldClock(ptr, clock) && clock.minuteOfDay == minute &&
                clock.daytime == (minute >= 360 && minute < 1080), "solar minute and sun/moon match day boundaries");
        }
        Put(daytimeState.data(), 0x28, 420 * ns); Put(daytimeState.data(), 0x30, 1200 * ns);
        Put(daytimeState.data(), 0x48, 1100 * ns);
        Check(ReadWorldClock(ptr, clock) && clock.daytime, "sun uses the world's configured daylight interval");
        for (auto bad : {-1LL, CLOCK_NANOSECONDS_PER_DAY, 0x7FFFFFFFFFFFFFFFLL})
        {
            Put(daytimeState.data(), 0x48, bad);
            Check(!ReadWorldClock(ptr, clock) && !clock.valid, "invalid time is hidden instead of formatted");
        }
        Check(!ReadWorldClock(0, clock) && !ReadWorldClock(0x10001, clock), "missing time object is safely rejected");
        Put(daytimeState.data(), 0x48, 725 * ns); Put(daytimeState.data(), 0x28, 1250 * ns);
        Check(!ReadWorldClock(ptr, clock), "reversed daylight interval is rejected");
        Put(daytimeState.data(), 0x28, 360 * ns); Put(daytimeState.data(), 0x30, 1080 * ns);
        const auto savedInit = g_iterInit;
        g_iterInit = InitDaytime; g_clockLayoutSupported = true; g_showClock = true;
        uintptr_t ctx[] = {0x1234, 7};
        auto capture = [&]() { g_lastClockCaptureTick = GetTickCount() - 100; CaptureDaytimeUiHook(ctx, nullptr, nullptr, nullptr); };
        capture();
        Check(ctx[1] == 7 && CopyWorldClock(clock, GetTickCount()) && clock.minuteOfDay == 725,
            "clock hook uses private iterator and copies only values");
        Put(daytimeState.data(), 0x48, 360 * ns); capture();
        Check(CopyWorldClock(clock, GetTickCount()) && clock.minuteOfDay == 360,
            "sleep or server time jumps are reflected immediately without wall-clock interpolation");
        Put(daytimeState.data(), 0x48, 1439 * ns); capture();
        Put(daytimeState.data(), 0x48, std::int64_t{0}); capture();
        Check(CopyWorldClock(clock, GetTickCount()) && clock.minuteOfDay == 0 && !clock.daytime, "midnight wraps to 00:00 and moon");
        Check(!CopyWorldClock(clock, GetTickCount() + 2001), "clock disappears when source stops updating");
        g_worldClock.tick = 0xFFFFFFF0;
        Check(CopyWorldClock(clock, 0x10), "clock freshness handles tick wrap");
        capture(); g_showClock = false;
        Check(!CopyWorldClock(clock, GetTickCount()), "show_clock hides current snapshot");
        g_showClock = true;
        Check(CopyWorldClock(clock, GetTickCount()), "show_clock restores fresh snapshot");
        ClearLiveMarkers();
        Check(!CopyWorldClock(clock, GetTickCount()), "world exit clears clock");
        capture(); ++g_worldSessionGeneration;
        Check(!CopyWorldClock(clock, GetTickCount()), "previous-session clock cannot appear in a new world");
        capture(); g_clockLayoutSupported = false;
        Check(!CopyWorldClock(clock, GetTickCount()), "unsupported clock layout never renders cached data");
        g_clockLayoutSupported = true;

        // Execute the actual entry trampoline against a synthetic function with
        // the verified prologue and a known return value. No game is modified.
        auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        Check(code != nullptr, "allocate isolated clock hook fixture");
        constexpr int offset = 128;
        std::memcpy(code + offset, g_daytimeUiExpected.data(), g_daytimeUiExpected.size());
        const std::uint8_t tail[] = {0x48,0x83,0xC4,0x60,0x41,0x5E,0xB8,0x7B,0,0,0,0xC3};
        std::memcpy(code + offset + g_daytimeUiExpected.size(), tail, sizeof(tail));
        const auto savedBase = g_exeBase; g_exeBase = reinterpret_cast<uintptr_t>(code);
        auto* hook = InstallEntryHook(offset, g_daytimeUiExpected, reinterpret_cast<void*>(&CaptureDaytimeUiHook), "clock fixture");
        Check(hook && ActivateHook(hook), "clock entry trampoline installs on verified whole instructions");
        FlushInstructionCache(GetCurrentProcess(), code, 4096);
        using Fixture = int(__fastcall*)(void*);
        ClearLiveMarkers(); g_lastClockCaptureTick = GetTickCount() - 100;
        Check(reinterpret_cast<Fixture>(code + offset)(ctx) == 123 && ctx[1] == 7 && CopyWorldClock(clock, GetTickCount()),
            "real clock trampoline preserves stack, context and original return");
        hook->deactivate();
        Check(std::memcmp(code + offset, g_daytimeUiExpected.data(), g_daytimeUiExpected.size()) == 0,
            "clock hook removal restores original instructions");
        auto allocation = reinterpret_cast<void*>(hook->shellcode->data->address);
        delete hook->shellcode; delete hook->patch; delete hook;
        VirtualFree(allocation, 0, MEM_RELEASE); VirtualFree(code, 0, MEM_RELEASE);
        g_exeBase = savedBase; g_iterInit = savedInit;

        Check(TryLoadMinimapFrameFromPath("assets/embervale_minimap_frame.rgba", g_minimapFrame), "load actual compass frame for layout bounds");
        g_minimapFrame.attempted = true;
        for (int height : {480,720,1080,1439,1440,2160})
        {
            const int radius = ClampValue(height / 11, 104, 142);
            const int extra = MinimapRasterFrameExtra(radius);
            const int half = MinimapRasterVisibleHalfHeight(radius, extra);
            const int yOffset = -MaxValue(1, (2 * (radius + extra) + 128) / 256);
            const int topExtra = half - yOffset - radius;
            const int bottomExtra = half + yOffset - radius;
            const int footer = bottomExtra + CLOCK_FRAME_GAP + 2 * CLOCK_PANEL_HALF_HEIGHT;
            for (auto placement : {MinimapPlacement::TopRight,MinimapPlacement::MiddleRight,MinimapPlacement::BottomRight})
            {
                g_minimapPlacement = static_cast<int>(placement);
                for (bool showClock : {false, true})
                {
                    const int cy = ComputeMinimapCenterY(height, radius, MaxValue(52,height/42), showClock ? footer : bottomExtra, topExtra);
                    Check(cy - half + yOffset >= 8 && cy + radius + (showClock ? footer : bottomExtra) <= height - 8,
                        "complete frame and optional clock fit every placement and screen height");
                    if (placement == MinimapPlacement::TopRight)
                        Check(cy - half + yOffset >= height / 9 + 8, "top-right frame leaves the quest title and Journal region clear");
                }
            }
            Check(CLOCK_FRAME_GAP < 0 && CLOCK_FRAME_GAP >= -8 && bottomExtra + CLOCK_FRAME_GAP >= 8,
                "clock slightly overlaps the ornament while staying outside the map circle");
            // The measured circular bound includes every opaque pixel at every
            // compass angle. Sample actual pixel corners, independently of the
            // production bound calculation, to guard against ornament overlap.
            bool containsAll = true;
            const auto& frame = g_minimapFrame;
            for (int y=0; y<frame.height; ++y) for (int x=0; x<frame.width; ++x)
            {
                if (!frame.rgba[(y * frame.width + x) * 4 + 3]) continue;
                const float dx = std::abs(x + 0.5f - frame.width * 0.5f) + 0.5f;
                const float dy = std::abs(y + 0.5f - frame.height * 0.5f) + 0.5f;
                if (std::hypot(dx,dy) * (2 * (radius + extra)) / frame.width > half) containsAll = false;
            }
            Check(containsAll, "clock clearance contains real frame ornaments at every rotation");
        }
        g_minimapPlacement = static_cast<int>(MinimapPlacement::BottomRight);
        clock.valid = true; clock.minuteOfDay = 725; clock.daytime = true;
        std::memset(pixels, 0, sizeof(pixels)); clears = 0;
        DrawWorldClockPanel(renderer, nullptr, 256, 160, clock);
        Check(pixels[155][236].r > 0.9f && pixels[160][212].r == 1.0f && clears < 30,
            "weather-hidden clock retains time and its fallback sun icon");
        Check(pixels[142][256].r > 0 && pixels[139][256].r == 0, "clock frame has bounded vertical footprint");
        clock.daytime = false;
        DrawWorldClockPanel(renderer, nullptr, 256, 160, clock);
        Check(pixels[160][207].b > 0.9f && pixels[157][216].r < 0.1f,
            "weather-hidden clock retains a crescent moon at night");
        clock.valid = false; clears = 0;
        DrawWorldClockPanel(renderer, nullptr, 256, 160, clock);
        Check(clears == 0, "invalid clock emits no drawing commands");
        ClearLiveMarkers();
    }

    float fixtureWeather[4] = {0, 1, 0, 0};
    int weatherCalls = 0;
    bool weatherArguments = false;
    float* __fastcall WeatherFixture(float* output, void* weather, const void* position, std::int64_t time)
    {
        ++weatherCalls;
        weatherArguments = weather == reinterpret_cast<void*>(0x2222) &&
            position == reinterpret_cast<void*>(0x3333) && time == 0x123456789abcdefLL;
        std::memcpy(output, fixtureWeather, sizeof(fixtureWeather));
        return output;
    }

    void CheckWeather(VulkanMinimapRenderer& renderer)
    {
        WorldWeatherSnapshot weather{};
        float values[4] = {};
        for (int i=0; i<4; ++i)
        {
            std::fill(std::begin(values), std::end(values), 0.0f); values[i]=1;
            Check(ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather) && weather.kind==i,
                "weather reader identifies each game-defined blend channel");
            Check(std::strlen(WeatherName(static_cast<WeatherKind>(weather.kind)))>=4,
                "each weather state has a readable label");
        }
        values[0]=0.2f; values[1]=0.3f; values[2]=0.5f; values[3]=0;
        Check(ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather) && weather.kind==2,
            "blended conditions choose the strongest local weather state");
        for (float invalid : {NAN, INFINITY, -0.1f, 1.1f})
        {
            values[0]=invalid;
            Check(!ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather) && !weather.valid,
                "nonfinite and out-of-range weather weights fail closed");
        }
        std::fill(std::begin(values),std::end(values),0.0f);
        Check(!ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather), "empty blend is unknown, never fabricated clear weather");
        std::fill(std::begin(values),std::end(values),1.0f);
        Check(!ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather), "invalid blend total is rejected");
        Check(!ReadWorldWeather(1, weather), "unreadable weather sample is rejected");

        const auto savedBase=g_exeBase, savedSize=g_exeImageSize;
        const auto savedSampler=g_sampleWeather;
        auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        Check(code!=nullptr,"allocate isolated weather call-site fixture");
        // A real caller with Windows x64 shadow space, then a leaf target relay.
        const std::uint8_t caller[]={0x48,0x83,0xEC,0x28,0xE8,55,0,0,0,0x48,0x83,0xC4,0x28,0xC3};
        std::memcpy(code,caller,sizeof(caller));
        const std::uint8_t jump[]={0xFF,0x25,0,0,0,0};
        std::memcpy(code+64,jump,sizeof(jump));
        Put(code,70,reinterpret_cast<uintptr_t>(&WeatherFixture));
        g_exeBase=reinterpret_cast<uintptr_t>(code); g_exeImageSize=4096;
        Check(InstallWeatherSampleHook(4,65)==nullptr,"weather hook rejects a different original call target");
        code[4]=0xE9;
        Check(InstallWeatherSampleHook(4,64)==nullptr,"weather hook requires CALL, never accepts an unrelated jump");
        code[4]=0xE8;
        auto* hook=InstallWeatherSampleHook(4,64);
        Check(hook && ActivateHook(hook),"weather wrapper installs at the verified call site");
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        auto run=[&]() {
            g_lastWeatherCaptureTick=GetTickCount()-100;
            return reinterpret_cast<SampleWeatherFn>(code)(values,reinterpret_cast<void*>(0x2222),
                reinterpret_cast<void*>(0x3333),0x123456789abcdefLL);
        };
        g_weatherLayoutSupported=g_showWeather=true;
        ClearLiveMarkers(); weatherCalls=0;
        Check(run()==values && weatherCalls==1 && weatherArguments && std::memcmp(values,fixtureWeather,sizeof(values))==0,
            "actual relay preserves all four arguments, stack, return pointer and original output");
        Check(CopyWorldWeather(weather,GetTickCount()) && weather.kind==1,"wrapper captures completed local weather result");
        Check(!CopyWorldWeather(weather,weather.tick+2001),"stale weather disappears instead of sticking indefinitely");
        g_worldWeather.tick=0xfffffff0u;
        Check(CopyWorldWeather(weather,0x10),"weather freshness handles tick wrap");
        run(); g_showWeather=false;
        Check(!CopyWorldWeather(weather,GetTickCount()),"weather visibility option keeps snapshot hidden");
        Check(run()==values && weatherCalls==3,"hidden weather still preserves the original ambient sampler");
        g_showWeather=true;
        Check(CopyWorldWeather(weather,GetTickCount()),"weather visibility restores fresh data");
        ClearLiveMarkers();
        Check(!CopyWorldWeather(weather,GetTickCount()),"world exit clears weather");
        run(); ++g_worldSessionGeneration;
        Check(!CopyWorldWeather(weather,GetTickCount()),"previous-world weather cannot survive a session change");
        fixtureWeather[0]=1; fixtureWeather[1]=0; run();
        Check(CopyWorldWeather(weather,GetTickCount()) && weather.kind==0,"weather updates to clear after rain ends");
        fixtureWeather[0]=NAN; run();
        Check(!CopyWorldWeather(weather,GetTickCount()),"invalid new sample clears the previous valid state");
        fixtureWeather[0]=1;
        g_weatherLayoutSupported=false;
        const int before=weatherCalls;
        Check(run()==values && weatherCalls==before+1 && !CopyWorldWeather(weather,GetTickCount()),
            "disabled weather support forwards game behavior unchanged");
        DestroyCallSiteHook(hook);
        Check(!hook && std::memcmp(code,caller,sizeof(caller))==0,"removal restores the original call and frees the relay");
        Check(reinterpret_cast<SampleWeatherFn>(code)(values,reinterpret_cast<void*>(0x2222),
            reinterpret_cast<void*>(0x3333),0x123456789abcdefLL)==values,"original fixture runs normally after hook removal");
        VirtualFree(code,0,MEM_RELEASE); g_exeBase=savedBase; g_exeImageSize=savedSize; g_sampleWeather=savedSampler;

        WorldClockSnapshot clock{}; clock.valid=true; clock.minuteOfDay=12*60+35; clock.daytime=true;
        for (int i=0; i<4; ++i)
        {
            weather={}; weather.valid=true; weather.kind=static_cast<std::uint8_t>(i);
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather);
            Check(clears<25 && pixels[155][157].r>0.9f && pixels[155][272].r>0.9f,
                "compact weather panel retains time and its weather label in bounded batches");
            bool clipped=false;
            for (int y=0;y<320;++y) for(int x=0;x<512;++x)
                if(pixels[y][x].r!=0 && (y<142 || y>178 || x<124 || x>388)) clipped=true;
            Check(!clipped,"every weather label and icon fits the clock frame");
        }
        weather.valid=false; std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather);
        Check(pixels[160][136].r==0 && pixels[155][236].r>.9f && pixels[160][212].r==1,
            "missing weather retains the compact clock and its single fallback sun");
        g_weatherLayoutSupported=true; ClearLiveMarkers();
    }

    int fogCalls = 0;
    bool fogArguments = false;
    NativeFogHeader* fixtureFog = nullptr;
    float fogPosition[2] = {252,260};
    void __fastcall FogFixture(void* fog, const float* position, float radius)
    {
        ++fogCalls;
        fogArguments = fog == fixtureFog && position == fogPosition && radius == 37.5f;
        if (fixtureFog->data) reinterpret_cast<std::uint8_t*>(fixtureFog->data)[0] = 255;
    }

    void CheckLiveFog(VulkanMinimapRenderer& renderer)
    {
        std::vector<std::uint8_t> bytes(4*1024);
        NativeFogHeader header{512,512,2,2,0,reinterpret_cast<uintptr_t>(bytes.data()),4};
        const auto address = reinterpret_cast<uintptr_t>(&header);
        LiveFogSnapshot fog;
        bytes[0]=255; bytes[1024]=192; bytes[2048]=128; bytes[3072]=64;
        Check(ReadLiveFog(address,fog),"native tile-major fog header and bounded grid decode");
        Check(SampleLiveFog(&fog,4,508)==1 && SampleLiveFog(&fog,260,508)==192/255.0f &&
            SampleLiveFog(&fog,4,252)==128/255.0f && SampleLiveFog(&fog,260,252)==64/255.0f,
            "all four tile origins match native orientation and stride");
        bytes[31]=255; bytes[32]=128;
        ReadLiveFog(address,fog);
        Check(SampleLiveFog(&fog,252,508)==1 && SampleLiveFog(&fog,4,500)==128/255.0f,
            "within-tile row stride is 32 bytes");
        Check(std::fabs(SampleLiveFog(&fog,256,508)-(255+192)/510.0f)<0.00001f,
            "bilinear coverage is continuous across tile boundaries");
        Check(SampleLiveFog(&fog,0,512)==1 && SampleLiveFog(&fog,512,0)==0,
            "world edges clamp to last pixel without crossing tile storage");
        Check(SampleLiveFog(&fog,-1,2)==0 && SampleLiveFog(&fog,513,2)==0 &&
            SampleLiveFog(&fog,NAN,2)==0 && SampleLiveFog(nullptr,4,4)==0,
            "outside world and unavailable fog never reveal terrain");
        header.worldHeight=768; header.rows=3; header.tileCount=6; bytes.resize(6*1024);
        header.data=reinterpret_cast<uintptr_t>(bytes.data()); bytes[5*1024]=255;
        Check(ReadLiveFog(address,fog) && SampleLiveFog(&fog,260,252)==1,
            "rectangular world uses its own height for north-up sampling");
        auto good=header;
        for (float value : {0.0f,-1.0f,NAN,INFINITY,65536.0f})
        {
            header.worldHeight=value;
            Check(!ReadLiveFog(address,fog) && fog.tiles.empty(),"invalid world extent rejects fog without retaining old bytes");
        }
        header=good; header.tileCount=9999999;
        Check(!ReadLiveFog(address,fog),"oversized or mismatched tile allocation rejected before allocation");
        header=good; header.columns=129;
        Check(!ReadLiveFog(address,fog),"fog grid dimensions have a fixed upper bound");
        header=good; header.rows=0;
        Check(!ReadLiveFog(address,fog),"empty tile grid rejected");
        header=good; header.data=0x10001;
        Check(!ReadLiveFog(address,fog) && fog.tiles.empty(),"unreadable fog bytes fail closed");
        Check(!ReadLiveFog(0x10001,fog),"unreadable fog header rejected");
        header=good;
        const auto savedBase=g_exeBase, savedSize=g_exeImageSize;
        const auto savedUpdate=g_updateFog;
        auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        Check(code!=nullptr,"allocate isolated native fog call fixture");
        const std::uint8_t caller[]={0x48,0x83,0xEC,0x28,0xE8,55,0,0,0,0x48,0x83,0xC4,0x28,0xC3};
        const std::uint8_t jump[]={0xFF,0x25,0,0,0,0};
        std::memcpy(code,caller,sizeof(caller)); std::memcpy(code+64,jump,sizeof(jump));
        Put(code,70,reinterpret_cast<uintptr_t>(&FogFixture));
        g_exeBase=reinterpret_cast<uintptr_t>(code); g_exeImageSize=4096;
        auto* hook=InstallVerifiedCallHook(4,64,reinterpret_cast<uintptr_t>(&CaptureFogUpdate));
        g_updateFog=reinterpret_cast<UpdateFogFn>(code+64);
        Check(hook && ActivateHook(hook),"fog wrapper installs at original CALL site");
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        fixtureFog=&header; g_liveFogLayoutSupported=g_showFogOfWar=true;
        auto run=[&]() { g_lastFogCaptureTick=GetTickCount()-500;
            reinterpret_cast<UpdateFogFn>(code)(&header,fogPosition,37.5f); };
        ClearLiveMarkers(); bytes[0]=0; fogCalls=0; run();
        auto snapshot=CopyLiveFog(GetTickCount());
        Check(fogCalls==1 && fogArguments && snapshot && snapshot->tiles[0]==255,
            "real call relay preserves pointer and XMM2 radius and captures after the original update");
        bytes[0]=0;
        Check(snapshot->tiles[0]==255,"render snapshot owns bytes independently of game storage");
        reinterpret_cast<UpdateFogFn>(code)(&header,fogPosition,37.5f);
        Check(fogCalls==2 && CopyLiveFog(GetTickCount())==snapshot,"capture throttle still calls native exploration on every frame");
        Check(!CopyLiveFog(snapshot->tick+2001),"stale exploration snapshot cannot reveal terrain");
        auto wrapped=std::make_shared<LiveFogSnapshot>(*snapshot); wrapped->tick=0xfffffff0u; g_liveFog=wrapped;
        Check(CopyLiveFog(0x10)!=nullptr,"fog freshness handles tick wrap");
        run(); g_showFogOfWar=false; const auto old=g_liveFog; run();
        Check(!CopyLiveFog(GetTickCount()) && g_liveFog==old && fogArguments,
            "fog disabled skips copying but native exploration continues");
        g_showFogOfWar=true; run();
        Check(CopyLiveFog(GetTickCount())!=nullptr,"fog can be re-enabled live without restarting");
        ++g_worldSessionGeneration;
        Check(!CopyLiveFog(GetTickCount()),"previous-character exploration cannot leak into another session");
        run(); ClearLiveMarkers();
        Check(!CopyLiveFog(GetTickCount()),"world exit clears exploration snapshot");
        run(); header.data=0; run();
        Check(!CopyLiveFog(GetTickCount()),"invalid new grid removes old exploration snapshot");
        header=good; run(); g_liveFogLayoutSupported=false;
        Check(!CopyLiveFog(GetTickCount()),"unsupported executable never uses cached exploration");
        DestroyCallSiteHook(hook);
        Check(std::memcmp(code,caller,sizeof(caller))==0,"fog hook removal restores original native call");
        VirtualFree(code,0,MEM_RELEASE); g_exeBase=savedBase; g_exeImageSize=savedSize; g_updateFog=savedUpdate;
        g_liveFogLayoutSupported=true;

        const auto savedMap=g_realMap; const auto savedStep=g_minimapMapSampleStep.load();
        g_realMap={}; g_realMap.loaded=g_realMap.attempted=true; g_realMap.width=g_realMap.height=2;
        g_realMap.rgba.assign(16,230); g_minimapMapSampleStep=1;
        auto drawFog=std::make_shared<LiveFogSnapshot>();
        drawFog->worldWidth=drawFog->worldHeight=512; drawFog->columns=drawFog->rows=2;
        drawFog->tiles.assign(4096,0); drawFog->tick=GetTickCount(); drawFog->session=g_worldSessionGeneration;
        for(int tile : {1,3}) std::fill(drawFog->tiles.begin()+tile*1024,drawFog->tiles.begin()+(tile+1)*1024,255);
        g_liveFog=drawFog;
        for(float heading : {0.0f,1.5707963f,3.1415926f,4.7123889f}) for(float zoom : {0.5f,2.0f})
        {
            std::memset(pixels,0,sizeof(pixels));
            DrawRealMap(renderer,nullptr,256,160,60,256,256,zoom,heading);
            const int x=static_cast<int>(256+std::cos(heading)*30), y=static_cast<int>(160-std::sin(heading)*30);
            const int bx=512-x, by=320-y;
            Check(pixels[y][x].r>0.5f && pixels[by][bx].r<0.35f,
                "terrain fog remains attached to world coordinates at each heading and zoom");
        }
        g_showFogOfWar=false; DrawRealMap(renderer,nullptr,256,160,60,256,256,1,0);
        Check(pixels[160][226].r>0.5f,"fog-off draw reveals the original terrain");
        g_showFogOfWar=true; g_liveFog.reset(); DrawRealMap(renderer,nullptr,256,160,60,256,256,1,0);
        Check(pixels[160][226].r<0.35f && pixels[160][286].r<0.35f,"missing snapshot masks terrain instead of revealing the world");
        float r=.9f,g=.8f,b=.7f; ApplyExplorationFog(r,g,b,1);
        Check(std::fabs(r-.9f)<0.00001f && std::fabs(g-.8f)<0.00001f,"fully explored terrain keeps its original colour");
        ApplyExplorationFog(r,g,b,0);
        Check(r==.30f && g==.31f && b==.28f,"unexplored terrain is fully hidden by neutral fog");
        g_realMap=savedMap; g_minimapMapSampleStep=savedStep; g_showFogOfWar=false; ClearLiveMarkers();
    }

    void CheckFoggedIcons(VulkanMinimapRenderer& renderer)
    {
        ClearLiveMarkers();
        auto grid=std::make_shared<LiveFogSnapshot>();
        grid->worldWidth=grid->worldHeight=512; grid->columns=grid->rows=2;
        grid->tiles.assign(4096,0);
        for(int tile : {1,3}) std::fill(grid->tiles.begin()+tile*1024,grid->tiles.begin()+(tile+1)*1024,255);
        grid->tick=GetTickCount(); grid->session=g_worldSessionGeneration;
        g_liveFogLayoutSupported=g_showFogOfWar=true; g_liveFog=grid;
        const auto fog=CaptureMinimapFogFrame();
        const std::vector<MinimapWorldPoint> locations={
            {100,252,12},{100,252,31},{100,252,0xABCDEF01u},
            {400,252,12},{400,252,31},{400,252,0xABCDEF01u}};
        auto points=locations;
        FilterFoggedWorldPoints(points,fog);
        Check(points.size()==3 && std::all_of(points.begin(),points.end(),[](const auto& p){return p.x==400;}),
            "fog hides NPCs, original world locations and native location icons only at unexplored world positions");
        points={{100,252,MAP_MARKER_QUEST_KIND},{100,252,MAP_MARKER_QUEST_IMPORTANT_KIND},
            {100,252,10},{100,252,11},{100,252,13},{100,252,0xABCDEF01u,true},{100,252,31,false,true}};
        FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==7,"custom pins, quests and fallback navigation stay visible even when exploration data is missing");
        points=locations; FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.empty(),"missing fog data hides location icons consistently with covered terrain");
        points=locations; FilterFoggedWorldPoints(points,{false,nullptr});
        Check(points.size()==locations.size(),"disabling fog restores every normal location icon without a grid");
        points={{252,508,31},{260,508,31}};
        FilterFoggedWorldPoints(points,fog);
        Check(points.size()==1 && points[0].x==260,"fog icon edge follows tile boundary without leaking concealed locations");
        grid->tick=GetTickCount()-2001; points=locations;
        FilterFoggedWorldPoints(points,CaptureMinimapFogFrame());
        Check(points.empty(),"stale grid cannot leave icons visible over opaque terrain");
        grid->tick=GetTickCount();
        ++g_worldSessionGeneration; points=locations;
        FilterFoggedWorldPoints(points,CaptureMinimapFogFrame());
        Check(points.empty(),"previous-world explored icons disappear after a session change");
        grid->session=g_worldSessionGeneration;
        auto revealed=std::make_shared<LiveFogSnapshot>(*grid); revealed->tiles.assign(4096,255);
        g_liveFog=revealed; points=locations;
        FilterFoggedWorldPoints(points,CaptureMinimapFogFrame());
        Check(points.size()==6,"new exploration reveals previously hidden icons on the next frame");
        points=locations; FilterFoggedWorldPoints(points,fog);
        Check(points.size()==3,"one captured frame keeps terrain and icons on the same immutable snapshot");
        const auto savedLimit=g_minimapMaxDrawnPoints.load(); g_minimapMaxDrawnPoints=8;
        points.clear(); for(int i=0;i<16;++i) points.push_back({100.0f+i,252,31});
        for(int i=0;i<8;++i) points.push_back({400.0f+i,252,12});
        FilterFoggedWorldPoints(points,fog); LimitMinimapWorldPoints(points,100,252);
        Check(points.size()==8 && points[0].x>=400,"hidden nearby icons do not consume the visible icon budget");
        g_minimapMaxDrawnPoints=savedLimit;

        // Native quest types remain recognizable without matching atlas artwork.
        g_worldMapLayoutSupported=g_showWorldMarkers=true; g_worldMapValid=true; g_worldMapTick=GetTickCount();
        g_worldMapMarkers={{Fixed(100),0,Fixed(200),0,true},
            {Fixed(100),0,Fixed(200),MAP_MARKER_QUEST_IMPORTANT_KIND,false},
            {Fixed(130),0,Fixed(200),MAP_MARKER_QUEST_KIND,false}};
        points.clear(); BuildWorldPoints(points,{}, {}, {}); FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==2 && points[0].kind==MAP_MARKER_QUEST_IMPORTANT_KIND && points[1].kind==MAP_MARKER_QUEST_KIND,
            "both native quest markers survive fog and a coincident custom pin");
        std::memset(pixels,0,sizeof(pixels)); clears=0;
        for(const auto& p:points) CmdClearPoiIcon(renderer,nullptr,256,160,130,static_cast<int>(p.x)+100,160,p.kind,false);
        Check(clears>0,"quest markers still issue visible drawing commands through fog");

        // The navigation draw layer must remain entirely independent of fog.
        g_liveMarkerLayoutSupported=g_showOtherPlayers=g_showPings=g_showWaypoints=g_showDeathMarkers=true;
        g_deathMarkerLayoutSupported=true; g_liveFog.reset();
        g_remotePlayers={{1,Fixed(260),0,Fixed(220),GetTickCount(),0}};
        g_pings={{2,Fixed(220),0,Fixed(220),GetTickCount(),0}};
        g_deathMarkers={{Fixed(220),0,Fixed(260),0x1AD8F96Eu,false}};
        CapturedWaypoint waypoint{}; waypoint.x=Fixed(260); waypoint.z=Fixed(260);
        std::memset(pixels,0,sizeof(pixels)); clears=0;
        DrawLiveMarkers(renderer,nullptr,{waypoint},240,240,1,0,256,160,100);
        auto anyInk=[](int cx,int cy) {
            for(int y=cy-15;y<=cy+15;++y) for(int x=cx-15;x<=cx+15;++x)
                if(pixels[y][x].r>0.5f || pixels[y][x].g>0.5f || pixels[y][x].b>0.5f) return true;
            return false;
        };
        Check(anyInk(276,180) && anyInk(236,180) && anyInk(236,140) && anyInk(276,140),
            "players, pings, tombstones and waypoint outlines render through fully opaque fog");
        g_showFogOfWar=false; ClearLiveMarkers();
    }

    void CheckCustomPinOutlines(VulkanMinimapRenderer& renderer)
    {
        for(bool pinFirst : {false,true})
        {
            std::vector<MinimapWorldPoint> points;
            const MinimapWorldPoint location{250,250,31}, pin{250,250,11};
            PushWorldPointUnique(points,pinFirst?pin:location);
            PushWorldPointUnique(points,pinFirst?location:pin);
            Check(points.size()==1 && points[0].kind==31 && points[0].customPin,
                "pin-on-marker preserves the original icon in either capture order");
            FilterFoggedWorldPoints(points,{true,nullptr});
            Check(points.size()==1,"pinned location retains its underlying icon through fog");
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            DrawWorldPointIcons(renderer,nullptr,points,250,250,1,0,256,160,100,100);
            Check(pixels[160][274].r==1 && pixels[160][274].g<0.3f && pixels[160][256].r>0,
                "production icon draw keeps the location artwork and adds the red outline");
        }
        std::vector<MinimapWorldPoint> points;
        PushWorldPointUnique(points,{250,250,11});
        Check(points.size()==1 && points[0].kind==11 && !points[0].customPin,
            "custom pin on empty terrain retains the standalone flag");
        std::memset(pixels,0,sizeof(pixels)); clears=0;
        DrawWorldPointIcons(renderer,nullptr,points,250,250,1,0,256,160,100,100);
        Check(pixels[154][259].r>0.9f && pixels[160][274].r==0,"standalone pin draws its flag without a marker outline");

        std::memset(pixels,0,sizeof(pixels));
        CmdClearRect(renderer,nullptr,.4f,.7f,.9f,1,253,157,7,7);
        DrawCustomPinOutline(renderer,nullptr,256,160);
        Check(pixels[160][256].b==.9f && pixels[160][260].r==0,
            "red outline has a transparent interior and preserves original icon pixels");
        DrawWaypointDiamond(renderer,nullptr,256,160);
        Check(pixels[160][271].g==.86f && pixels[160][274].g==.20f && pixels[160][256].b==.9f,
            "yellow selected-waypoint edge and red custom-pin edge remain separately visible");

        g_worldMapLayoutSupported=g_showWorldMarkers=true; g_worldMapValid=true; g_worldMapTick=GetTickCount();
        g_worldMapMarkers={{Fixed(250),0,Fixed(250),0,true},
            {Fixed(250),0,Fixed(250),WORLD_MAP_FLAME_ALTAR,false}};
        points.clear(); BuildWorldPoints(points,{}, {}, {});
        Check(points.size()==1 && points[0].kind==31 && points[0].customPin,"completed native map lists produce a pinned altar icon");
        g_worldMapMarkers.erase(g_worldMapMarkers.begin());
        points.clear(); BuildWorldPoints(points,{}, {}, {});
        Check(points.size()==1 && points[0].kind==31 && !points[0].customPin,
            "removing a custom pin clears its outline while preserving the original location");
        FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==1 && points[0].navigation && !points[0].customPin,
            "placed Flame Altar remains visible after its custom pin is removed");
        g_worldMapMarkers={{Fixed(250),0,Fixed(250),WORLD_MAP_FLAME_ALTAR,false},
            {Fixed(300),0,Fixed(250),0xABCDEF01u,false},
            {Fixed(350),0,Fixed(250),0,false}};
        points.clear(); BuildWorldPoints(points,{}, {}, {}); FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==1 && points[0].kind==31 && points[0].navigation,
            "only the placed altar bypasses fog while original world locations and NPCs remain hidden");
        ClearLiveMarkers();
    }

    void CheckIndependentPanels(VulkanMinimapRenderer& renderer)
    {
        WorldClockSnapshot clock{}; clock.valid=true; clock.daytime=true; clock.minuteOfDay=12*60+35;
        WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=3;
        for(bool time : {false,true}) for(bool conditions : {false,true})
        {
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            weather.valid=conditions;
            DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather,time);
            Check((clears>0)==(time||conditions),"all four independent clock and weather visibility combinations render correctly");
            if (time!=conditions) Check(pixels[160][136].r==0 && pixels[160][256+(conditions?84:72)].r>0,
                "single-feature panel uses a frame sized for its content");
        }
        weather.valid=true; clock.valid=false; clears=0;
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather,false);
        Check(clears>0,"weather remains visible when clock source is unavailable");
        g_worldClock={}; g_worldClock.valid=true; g_worldClock.tick=GetTickCount();
        g_worldClock.session=g_worldSessionGeneration; g_showClock=false; g_clockLayoutSupported=true;
        Check(!CopyWorldClock(clock,GetTickCount()) && CopyWorldClock(clock,GetTickCount(),false),
            "hidden clock still supplies real daylight state to the weather icon");
        g_showClock=true; ClearLiveMarkers();
    }

    void CheckCombinedWeatherSymbols(VulkanMinimapRenderer& renderer)
    {
        for(int kind=0;kind<4;++kind) for(bool daytime : {true,false})
        {
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            DrawWeatherSymbol(renderer,nullptr,256,160,static_cast<WeatherKind>(kind),daytime);
            int celestial=0, precipitation=0; bool outside=false;
            for(int y=0;y<320;++y) for(int x=0;x<512;++x)
            {
                const auto& p=pixels[y][x];
                celestial += daytime ? p.r==1 && p.g==.78f : p.r==.74f && p.b==.98f;
                precipitation += p.r==.60f && p.b==1;
                outside |= p.r!=0 && (std::abs(x-256)>14 || std::abs(y-160)>12);
            }
            Check(celestial>0 && !outside && clears<=3,"each weather state has a bounded sun or moon variant");
            Check((precipitation>0)==(kind!=0),"clear skies omit precipitation while rain, snow and blizzard retain it");
            if(kind!=0) Check(pixels[159][252].r==.75f && pixels[159][252].b==.87f,
                "foreground cloud covers the lower sun or moon");
        }
        std::memset(pixels,0,sizeof(pixels));
        DrawWeatherSymbol(renderer,nullptr,256,160,WeatherKind::Clear,false);
        Check(pixels[160][251].b==.98f && pixels[160][259].r==0,"clear night has a crescent with an open cutout");
        std::memset(pixels,0,sizeof(pixels));
        DrawWeatherSymbol(renderer,nullptr,256,160,WeatherKind::Rain,false,false);
        Check(pixels[159][252].r==.75f && pixels[156][246].r==0,
            "unknown daylight keeps rain without fabricating a sun or moon");
        WorldClockSnapshot clock{}; clock.valid=true; clock.daytime=true; clock.minuteOfDay=12*60+35;
        WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=1;
        std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather);
        int celestialSlots=0; bool outsideWeatherSlot=false;
        for(int y=148;y<=172;++y) for(int x=132;x<=380;++x)
            if(pixels[y][x].r==1 && pixels[y][x].g==.78f)
            {
                ++celestialSlots;
                outsideWeatherSlot |= x<232 || x>260;
            }
        Check(celestialSlots>0 && !outsideWeatherSlot,"combined panel has one weather sun and no duplicate clock sun");
        std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,{},true);
        Check(pixels[160][212].r==1 && pixels[160][212].g==.78f,
            "weather-off panel retains its clock sun");
        clock.daytime=false; std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,{},true);
        Check(pixels[160][207].b==.98f && pixels[160][215].r<.1f,
            "weather-off panel retains its clock moon");
        weather.valid=true; std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather,false);
        Check(pixels[156][188].b==.98f && pixels[160][212].r<.1f,
            "weather-only panel retains the night variant with no clock icon");
    }

    void CheckClientLayout(const char* path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        Check(input.good(), "open optional client executable read-only");
        std::vector<char> file(static_cast<std::size_t>(input.tellg())); input.seekg(0);
        Check(input.read(file.data(), file.size()).good() && file.size() > sizeof(IMAGE_DOS_HEADER), "read client executable");
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        Check(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) < file.size(), "client PE header bounds");
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
        Check(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
            nt->OptionalHeader.SizeOfImage < 512 * 1024 * 1024, "client image is bounded x64 PE");
        std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto& s = sections[i];
            Check(static_cast<std::uint64_t>(s.PointerToRawData) + s.SizeOfRawData <= file.size() &&
                static_cast<std::uint64_t>(s.VirtualAddress) + s.SizeOfRawData <= image.size(), "client section bounds");
            std::memcpy(image.data() + s.VirtualAddress, file.data() + s.PointerToRawData, s.SizeOfRawData);
        }
        const auto savedBase = g_exeBase; const auto savedSize = g_exeImageSize; const auto savedInit = g_iterInit;
        g_exeBase = reinterpret_cast<uintptr_t>(image.data()); g_exeImageSize = image.size();
        for (const auto rva : { 0x1D45390, 0x1D47820, 0x1D4C100, 0x1D437D0, 0x1D469E0, 0x1D44550, 0x1D48D00 })
            for (int word : { 0, 2 })
            {
                uintptr_t value = 0; std::memcpy(&value, image.data() + rva + word * 8, 8);
                value = value - nt->OptionalHeader.ImageBase + g_exeBase;
                Put(image.data(), rva + word * 8, value);
            }
        for (int i=0;i<4;++i)
        {
            const auto rva=0x1AA54F0+i*0x28;
            uintptr_t value=0; std::memcpy(&value,image.data()+rva,8);
            Put(image.data(),rva,value-nt->OptionalHeader.ImageBase+g_exeBase);
        }
        g_iterInit = reinterpret_cast<IterInitFn>(g_exeBase + 0x8DA7C0);
        Check(HasVerifiedWorldMapLayout(), "production layout gate matches installed Steam executable");
        Check(HasVerifiedDeathMarkerLayout(), "death layout gate matches the installed Steam executable");
        Check(HasVerifiedWorldClockLayout(), "clock layout matches the installed Steam executable");
        Check(HasVerifiedLiveFogLayout(), "live exploration layout matches installed Steam client and local ownership gate");
        for (auto rva : {0x1D48D08,0x1D48D10,0x2A3F22,0x2A3F4B,0x2A3F80,0x320440,0x320EA4,0x321000,0x12B0CF4})
        {
            image[rva]^=1;
            Check(!HasVerifiedLiveFogLayout() && HasVerifiedWorldClockLayout(),
                "changed fog layout disables exploration capture independently of the clock");
            image[rva]^=1;
        }
        Check(HasVerifiedWorldWeatherLayout(), "weather layout matches installed Steam client, local-player guard and enum mapping");
        for (auto rva : {0x1D44558,0x1D44560,0x29C85D,0x29CBEA,0x29CBF8,0x9AE790,0x9AECE1,0x1AA5500,0x1C7E55C})
        {
            image[rva]^=1;
            Check(!HasVerifiedWorldWeatherLayout() && HasVerifiedWorldClockLayout(),
                "changed weather call, blend or enum disables weather but preserves the clock");
            image[rva]^=1;
        }
        for (auto rva : {0x266980, 0x266994, 0xCC3710, 0xCAA96E, 0xCE519A, 0x1D437D8, 0x1D469F0})
        {
            image[rva] ^= 1;
            Check(!HasVerifiedWorldClockLayout() && HasVerifiedWorldMapLayout(),
                "changed time layout disables clock without disabling map markers");
            image[rva] ^= 1;
        }
        for (auto rva : {0x29DE49, 0x2A9D8F, 0x2A9DB0, 0x29C3F3})
        {
            image[rva] ^= 1;
            Check(!HasVerifiedDeathMarkerLayout() && HasVerifiedWorldMapLayout(),
                "changed tombstone layout disables deaths without disabling POIs");
            image[rva] ^= 1;
        }
        image[0x2A0D34] ^= 1;
        Check(!HasVerifiedWorldMapLayout(), "changed marker stride disables the reader");
        image[0x2A0D34] ^= 1; image[0x29DE3B] ^= 1;
        Check(!HasVerifiedWorldMapLayout(), "changed UI array layout disables the reader");
        g_exeBase = savedBase; g_exeImageSize = savedSize; g_iterInit = savedInit;
    }
}

int main(int argc, char** argv)
{
    Check(TryLoadMinimapIconsFromPath("assets/embervale_minimap_icons.bin", g_minimapIcons), "load actual icon atlas");
    g_minimapIcons.attempted = true;
    const auto nativeIcon = std::find_if(g_minimapIcons.icons.begin(), g_minimapIcons.icons.end(), [](const auto& icon) { return icon.key > 0xFFFFu; });
    Check(nativeIcon != g_minimapIcons.icons.end(), "atlas contains native world-map keys");
    const auto iconKey = nativeIcon->key;
    Check(std::count(nativeIcon->glyph.begin(), nativeIcon->glyph.end(), 1) > 0 &&
        std::count(nativeIcon->glyph.begin(), nativeIcon->glyph.end(), 1) < 200, "native glyph excludes its dark tile background");
    Entry(world.data(), 0, 1000, 1000, WORLD_MAP_FLAME_ALTAR);
    Entry(world.data(), 1, 1100, 1000, 0);
    Entry(world.data(), 2, 1200, 1000, iconKey);
    Entry(world.data(), 3, 1300, 1000, WORLD_MAP_PING);
    Entry(world.data(), 4, 1400, 1000, 0xDEADBEEF);
    Entry(custom.data(), 0, 1500, 1000, iconKey);
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(world.data()));
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{5});
    Put(state.data(), CUSTOM_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(custom.data()));
    Put(state.data(), CUSTOM_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
    const DWORD now = GetTickCount();
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(CopyWorldMapMarkers(now).size() == 6, "both completed map lists are captured");
    g_worldMapLayoutSupported = g_liveMarkerLayoutSupported = true;
    std::vector<MinimapWorldPoint> points;
    BuildWorldPoints(points, {}, {}, {});
    Check(points.size() == 5, "world-map mirror excludes the dedicated live ping duplicate");
    const auto countKind = [&](std::uint32_t kind) { return std::count_if(points.begin(), points.end(), [kind](const auto& p) { return p.kind == kind; }); };
    Check(countKind(11) == 1 && countKind(12) == 1 && countKind(31) == 1 && countKind(iconKey) == 1 && countKind(55) == 1,
        "custom pin, NPC fallback, altar, native icon and unknown fallback remain distinct");
    Entry(custom.data(), 0, 1200, 1000, iconKey);
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    points.clear(); BuildWorldPoints(points, {}, {}, {});
    Check(points.size() == 4 && countKind(11) == 0 && countKind(iconKey) == 1 &&
        std::count_if(points.begin(),points.end(),[&](const auto& p){return p.kind==iconKey && p.customPin;})==1,
        "custom pin decorates the original coincident POI icon");
    Check(CopyWorldMapMarkers(now + 2001).empty(), "interrupted world-map feed expires");
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), 0xFFFFFFF0u);
    Check(CopyWorldMapMarkers(0x10).size() == 6, "world-map freshness survives tick wrap");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
    Put(state.data(), CUSTOM_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(g_worldMapValid && CopyWorldMapMarkers(now).empty(), "empty frame removes all earlier map markers");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{WORLD_MAP_MAX_ENTRIES + 1});
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(!g_worldMapValid && CopyWorldMapMarkers(now).empty(), "oversized array is rejected before allocation or traversal");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET, uintptr_t{1});
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(!g_worldMapValid, "unreadable marker pointer clears the feed");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(world.data()));
    Entry(world.data(), 0, -10000, 1000, iconKey);
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(g_worldMapValid && CopyWorldMapMarkers(now).empty(), "out-of-world coordinates are omitted");
    Entry(world.data(), 0, 1000, 1000, iconKey);
    uintptr_t context[] = { 0x1234, 7 };
    g_iterInit = InitMap; g_lastWorldMapCaptureTick = GetTickCount() - 100;
    CaptureWorldMapHook(context, nullptr, nullptr, nullptr);
    Check(context[1] == 7 && CopyWorldMapMarkers(GetTickCount()).size() == 1, "named entry hook uses a private iterator context");
    g_worldMapLayoutSupported = false;
    ClearLiveMarkers(); CaptureWorldMapHook(context, nullptr, nullptr, nullptr);
    Check(CopyWorldMapMarkers(GetTickCount()).empty(), "unsupported layout does not invoke the reader");
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now); ClearLiveMarkers();
    Check(CopyWorldMapMarkers(now).empty(), "world exit clears world-map snapshots");

    ModContext config{};
    config.config.GetString = [](const char*, const char* key, std::string fallback) {
        if (std::strcmp(key, "map_light") == 0) return std::string("150");
        if (std::strcmp(key, "heading_smoothing_ms") == 0) return std::string("0");
        if (std::strcmp(key, "icon_style") == 0) return std::string("world-map");
        if (std::strcmp(key, "show_clock") == 0) return std::string("false");
        if (std::strcmp(key, "show_fog_of_war") == 0) return std::string("false");
        if (std::strcmp(key, "show_weather") == 0) return std::string("false");
        if (std::strcmp(key, "show_death_markers") == 0) return std::string("false");
        if (std::strcmp(key, "show_world_markers") == 0) return std::string("false");
        return fallback;
    };
    RefreshMinimapConfig(&config);
    Check(g_minimapMapLight == 100 && g_headingSmoothingMs == 0 && g_worldMapIconStyle && !g_showWorldMarkers,
        "runtime config enables gold icons and applies bounded display options");
    Check(!g_showDeathMarkers, "runtime config hides death markers");
    Check(!g_showClock, "runtime config hides world clock");
    Check(!g_showFogOfWar, "fog option applies live without reinstalling");
    Check(!g_showWeather, "runtime config hides weather without reinstalling");
    Check(g_showOtherPlayers && g_showPings && g_showWaypoints, "world-map option preserves independent live-marker settings");
    config.config.GetString = [](const char*, const char* key, std::string fallback) {
        return std::strcmp(key, "heading_smoothing_ms") == 0 ? std::string("-1") : fallback;
    };
    RefreshMinimapConfig(&config);
    Check(g_headingSmoothingMs == 55, "invalid negative smoothing restores its default");
    Check(ParseConfigInteger("1.5", 55, 0, 100) == 55 && ParseConfigInteger("150junk", 55, 0, 100) == 55,
        "numeric config rejects punctuation and trailing garbage");
    Check(ParseConfigInteger("999999999999999999999", 55, 0, 100) == 100 && ParseConfigInteger(" 0 ", 55, 0, 100) == 0,
        "numeric config handles large values and surrounding whitespace");
    RefreshMinimapConfig(nullptr);
    Check(g_minimapMapLight == 55 && g_headingSmoothingMs == 55 && !g_worldMapIconStyle && g_showWorldMarkers,
        "original icons and default display settings are restored without reinstalling");

    Check(g_showDeathMarkers, "death markers default to enabled without configuration");
    Check(g_showClock, "world clock defaults to enabled without configuration");
    Check(g_showFogOfWar, "fog of war defaults to ON without configuration");
    Check(g_showWeather, "weather defaults to enabled without configuration");

    float r = 0.2f, g = 0.3f, b = 0.4f;
    ApplyMapLighting(r, g, b, 0.0f, 0);
    Check(std::fabs(r - 0.208f) < 0.0001f && std::fabs(b - 0.412f) < 0.0001f, "zero lighting retains original terrain treatment");
    float brightR = 0.2f, brightG = 0.3f, brightB = 0.4f;
    ApplyMapLighting(brightR, brightG, brightB, 0.0f, 55);
    Check(brightR > r && brightG > g && brightB > b && brightR <= 1, "default parchment lighting lifts terrain colors");
    r = g = b = 1.0f; ApplyMapLighting(r, g, b, 1.0f, 500);
    Check(r <= 1 && g <= 1 && b <= 1 && r > b, "lighting clamps input and warms the edge");
    VulkanMinimapRenderer renderer{};
    renderer.width = 512; renderer.height = 320; renderer.fns.cmdClearAttachments = Raster;
    CheckDeaths(renderer);
    CheckWeather(renderer);
    CheckWorldClock(renderer);
    CheckLiveFog(renderer);
    CheckFoggedIcons(renderer);
    CheckCustomPinOutlines(renderer);
    CheckIndependentPanels(renderer);
    CheckCombinedWeatherSymbols(renderer);
    Check(SmoothMapHeading(renderer, 3.1f, true, 100) == 3.1f, "first heading is immediate");
    Check(std::abs(SmoothMapHeading(renderer, -3.1f, true, 116)) > 3, "heading smoothing crosses the short arc at north");
    Check(SmoothMapHeading(renderer, 1, true, 2000) == 1, "stale heading resets after a pause");
    g_headingSmoothingMs = 0;
    Check(SmoothMapHeading(renderer, 2, true, 2001) == 2, "zero smoothing follows the camera immediately");
    g_headingSmoothingMs = 55; ClearLiveMarkers();
    Check(SmoothMapHeading(renderer, -1, true, 2002) == -1, "world re-entry resets smoothing");
    Check(SmoothMapHeading(renderer, 2, false, 2003) == 0, "missing heading returns to north");
    Check(MINIMAP_MAX_ZOOM == 7 && MINIMAP_MIN_ZOOM == -3, "zoom range includes four additional close-up steps");

    MinimapIcon glyph{}; glyph.width = glyph.height = 8; glyph.rgba.resize(8 * 8 * 4);
    for (int i : { 9, 10, 17, 18 }) for (int c = 0; c < 4; ++c) glyph.rgba[i * 4 + c] = 255;
    PrepareMapIconGlyph(glyph);
    Check(std::count(glyph.glyph.begin(), glyph.glyph.end(), 1) == 4, "transparent white glyph keeps its alpha shape");
    glyph.rgba.assign(8 * 8 * 4, 255); for (int c = 0; c < 3; ++c) glyph.rgba[9 * 4 + c] = 0;
    PrepareMapIconGlyph(glyph);
    Check(glyph.glyph[9] == 1 && glyph.glyph[0] == 0, "dark glyph on an opaque light tile is preserved");
    glyph.rgba.assign(8 * 8 * 4, 0); for (int i = 0; i < 64; ++i) glyph.rgba[i * 4 + 3] = 255;
    for (int c = 0; c < 3; ++c) glyph.rgba[9 * 4 + c] = 255;
    PrepareMapIconGlyph(glyph);
    Check(glyph.glyph[9] == 1 && glyph.glyph[0] == 0, "light glyph on an opaque dark tile is preserved");
    for (int y = 1; y < 7; ++y) for (int x = 1; x < 7; ++x)
        for (int c = 0; c < 3; ++c) glyph.rgba[(y * 8 + x) * 4 + c] = 255;
    PrepareMapIconGlyph(glyph);
    Check(glyph.glyph[9] == 1 && glyph.glyph[0] == 0, "large light glyph is not mistaken for the tile background");
    g_worldMapIconStyle = true;
    Check(TryDrawMinimapRasterIcon(renderer, nullptr, 64, 64, 40, 100, 64, iconKey, true), "actual atlas icon draws in the world-map style");
    bool outside = false;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
        if ((x-64)*(x-64) + (y-64)*(y-64) > 40*40 && pixels[y][x].r != 0) outside = true;
    Check(!outside, "gold diamond and glyph stay clipped to the circular map");
    g_worldMapIconStyle = false;
    Check(TryDrawMinimapRasterIcon(renderer, nullptr, 64, 64, 40, 64, 64, iconKey, false), "original icon style remains available");
    if (argc > 1) CheckClientLayout(argv[1]);
    if (argc > 2)
    {
        std::memset(pixels, 0, sizeof(pixels));
        g_worldMapIconStyle = true;
        Check(TryLoadRealMapFromPath("assets/embervale_realmap_1024.rgba", g_realMap), "load real terrain for CPU preview");
        g_realMap.attempted = true;
        DrawRealMap(renderer, nullptr, 160, 160, 130, 2200, 2200, 2.0f, 0);
        for (int i = 0; i < 5; ++i)
            CmdClearPoiIcon(renderer, nullptr, 160, 160, 130, 95 + i * 33, 130, (nativeIcon + i)->key, false);
        CmdClearPoiIcon(renderer, nullptr, 160, 160, 130, 120, 195, 11, false);
        CmdClearPoiIcon(renderer, nullptr, 160, 160, 130, 195, 195, 12, false);
        DrawWaypointDiamond(renderer, nullptr, 160, 160); DrawPingDiamond(renderer, nullptr, 195, 90);
        DrawDeathMarker(renderer, nullptr, 160, 160, 130, 85, 185, 0x1AD8F96E);
        // Separate compact preview of both clock states using production draws.
        WritePreview(argv[2]);
        std::memset(pixels, 0, sizeof(pixels));
        WorldClockSnapshot clock{}; clock.valid = true; clock.minuteOfDay = 9 * 60 + 42; clock.daytime = true;
        DrawWorldClockPanel(renderer, nullptr, 256, 80, clock);
        clock.minuteOfDay = 23 * 60 + 7; clock.daytime = false;
        DrawWorldClockPanel(renderer, nullptr, 256, 145, clock);
        WritePreview((std::string(argv[2]) + ".clock.bmp").c_str());
        // Preview every day/night state through the production panel renderer.
        for (bool day : {true,false})
        {
            std::memset(pixels,0,sizeof(pixels));
            for (int i=0;i<4;++i)
            {
                WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=static_cast<std::uint8_t>(i);
                clock.daytime=day; clock.minuteOfDay=day?12*60+35:23*60+7;
                DrawWorldClockPanel(renderer,nullptr,256,40+i*75,clock,weather);
            }
            WritePreview((std::string(argv[2])+(day?".weather-day.bmp":".weather-night.bmp")).c_str());
        }
        std::memset(pixels, 0, sizeof(pixels));
        Check(TryLoadMinimapFrameFromPath("assets/embervale_minimap_frame.rgba", g_minimapFrame), "load actual compass frame for clock preview");
        g_minimapFrame.attempted = true;
        const int previewRadius = 90, previewExtra = MinimapRasterFrameExtra(previewRadius);
        DrawRealMap(renderer, nullptr, 256, 138, MinimapRasterMapRadius(previewRadius, previewExtra), 2200, 2200, 2, 0);
        TryDrawMinimapRasterFrame(renderer, nullptr, 256, 138, previewRadius, previewExtra);
        DrawPingDiamond(renderer, nullptr, 300, 100);
        DrawWaypointDiamond(renderer, nullptr, 212, 173);
        CmdClearPlayerArrow(renderer, nullptr, 256, 138, previewRadius);
        clock.minuteOfDay = 12 * 60 + 35; clock.daytime = true;
        const int previewHalf = MinimapRasterVisibleHalfHeight(previewRadius, previewExtra);
        const int previewYOffset = -MaxValue(1, (2 * (previewRadius + previewExtra) + 128) / 256);
        WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=1;
        DrawWorldClockPanel(renderer, nullptr, 256, 138 + previewHalf + previewYOffset + CLOCK_FRAME_GAP + CLOCK_PANEL_HALF_HEIGHT, clock,weather);
        WritePreview((std::string(argv[2]) + ".combined.bmp").c_str());
    }
    return 0;
}
