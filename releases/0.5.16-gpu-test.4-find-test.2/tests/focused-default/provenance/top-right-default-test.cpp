#include "E:\Repos\Enshrouded-minimap\scratch\minimap-top-right-default-20261024\source\src\dllmain.cpp"
#include <iostream>

static void Check(bool condition, const char* label)
{
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
    std::cout << "PASS: " << label << '\n';
}

int main(int argc, char** argv)
{
    Check(argc == 2, "config fixture path supplied");
    Check(g_minimapPlacement.load() == static_cast<int>(MinimapPlacement::TopRight), "static initial placement defaults top-right");
    Check(g_minimapOrientation.load() == MinimapOrientation::NorthUp, "north-up default preserved");
    Check(g_minimapOffset.load() == 0, "offset default preserved");
    Check(g_minimapMapSampleStep.load() == 2, "sample-step default preserved");

    ModContext api{};
    api.config.GetString = [](const char*, const char* key, std::string fallback) {
        return std::strcmp(key, "position") == 0 ? std::string("bottom-right") : fallback;
    };

    g_shroudtopiaConfigPath = argv[1];
    RefreshMinimapConfig(&api, true);
    Check(g_minimapPlacement.load() == static_cast<int>(MinimapPlacement::MiddleLeft), "explicit saved file position overrides top-right default and API fallback");

    g_shroudtopiaConfigPath = std::string(argv[1]) + ".missing";
    RefreshMinimapConfig(&api, true);
    Check(g_minimapPlacement.load() == static_cast<int>(MinimapPlacement::BottomRight), "API position remains fallback when saved file is absent");
    std::cout << "FOCUSED_RESULT=PASS\n";
    return 0;
}