// Exercises scalar publication and guards on an inert PE mapping; no game code executes.
#include <windows.h>
#include "../src/client_1_0_profile.h"
#include "../src/menu_visibility_1_0.h"
#include <array>
#include <cstring>
#include <iostream>
#include <thread>

namespace
{
    void Check(bool ok, const char* message)
    {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
        std::cout << "PASS: " << message << '\n';
    }

    void CheckSnapshot()
    {
        MinimapMenu10::Snapshot snapshot;
        Check(!snapshot.AllowsGameplay(1000), "no capture hides the minimap");
        snapshot.Capture(1, 1000, true);
        Check(snapshot.AllowsGameplay(1000), "fresh native InGame screen permits gameplay");
        Check(snapshot.AllowsGameplay(1500) && !snapshot.AllowsGameplay(1501), "500ms inclusive freshness boundary");
        Check(!snapshot.AllowsGameplay(999), "clock rollback cannot revive a snapshot");
        for (unsigned screen = 0; screen <= 255; ++screen) {
            snapshot.Capture(static_cast<std::uint8_t>(screen), 2000, true);
            Check(snapshot.AllowsGameplay(2000) == (screen == 1), "only InGame permits gameplay across all byte values");
        }
        snapshot.Capture(1, 3000, true);
        snapshot.Capture(1, 3001, false);
        Check(!snapshot.AllowsGameplay(3001), "failed native read retires prior gameplay capture");
        snapshot.Capture(1, 4000, true);
        snapshot.Clear();
        Check(!snapshot.AllowsGameplay(4000) && !snapshot.AllowsGameplay(0), "load unload and session reset clear the capture");
        snapshot.Capture(1, 0, true);
        Check(snapshot.AllowsGameplay(0) && snapshot.AllowsGameplay(500) && !snapshot.AllowsGameplay(501), "zero tick is a valid fresh capture");
        constexpr std::uint64_t lastTick = UINT64_MAX >> 8;
        snapshot.Capture(1, lastTick, true);
        Check(snapshot.AllowsGameplay(lastTick) && snapshot.AllowsGameplay(lastTick + 500) &&
            !snapshot.AllowsGameplay(lastTick + 501), "last representable capture keeps exact age boundaries");
        snapshot.Capture(1, lastTick + 1, true);
        Check(!snapshot.AllowsGameplay(lastTick + 1), "packing overflow cannot truncate into a fresh timestamp");
        snapshot.Capture(1, UINT64_MAX, true);
        Check(!snapshot.AllowsGameplay(0), "timestamp wrap cannot revive invalid capture");
        snapshot.Capture(1, lastTick, true);
        Check(!snapshot.AllowsGameplay(0), "wrapped current time hides prior capture");

        snapshot.Capture(1, 0, true);
        std::atomic<bool> stop{false};
        std::atomic<bool> torn{false};
        std::thread writer([&] {
            for (unsigned i = 0; i < 100000; ++i) {
                snapshot.Capture(1, 0, true);
                snapshot.Capture(9, 1000, true);
            }
            stop.store(true);
        });
        while (!stop.load()) if (snapshot.AllowsGameplay(1000)) torn.store(true);
        writer.join();
        Check(!torn.load(), "screen and tick publish together without a torn gameplay admission");
    }

    void CheckFixture(const char* path)
    {
        const auto profile = MinimapClient10::IdentifyFile(path);
        const bool currentBuild = profile == MinimapClient10::Profile::Build26082781;
        const auto Rva = [=](std::uintptr_t old) { return MinimapClient10::Rva(profile, old); };
        Check(profile != MinimapClient10::Profile::Unsupported, "fixture has the exact current executable SHA256");
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        Check(input.good(), "fixture opens read only");
        std::vector<std::uint8_t> file(static_cast<std::size_t>(input.tellg()));
        input.seekg(0);
        Check(static_cast<bool>(input.read(reinterpret_cast<char*>(file.data()), file.size())), "fixture bytes read completely");
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
        std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& section = sections[i];
            Check(static_cast<std::size_t>(section.PointerToRawData) + section.SizeOfRawData <= file.size() &&
                static_cast<std::size_t>(section.VirtualAddress) + section.SizeOfRawData <= image.size(), "PE section bounds hold");
            std::memcpy(image.data() + section.VirtualAddress, file.data() + section.PointerToRawData, section.SizeOfRawData);
        }
        Check(MinimapMenu10::MatchGuards(image.data(), image.size(), nullptr, currentBuild), "current UI lifetime and read instructions match");
        Check(!MinimapMenu10::MatchGuards(nullptr, image.size(), nullptr, currentBuild), "null mapped image is rejected");
        Check(!MinimapMenu10::MatchGuards(image.data(), 0, nullptr, currentBuild), "empty mapped image is rejected");
        Check(!MinimapMenu10::MatchGuards(image.data(), image.size(), nullptr, !currentBuild), "opposite HUD profile rejects fixture");
        // Independent ranges from the static disassembly, not the production guard table.
        const std::pair<std::size_t, std::size_t> ranges[] = {
            {Rva(0xc4e0c0), 18}, {Rva(0xc4e0d2), 54}, {Rva(0xc4e108), 32},
            {Rva(0xbc0446), 7}, {Rva(0xbc0969), 7}, {Rva(0xbc078b), 37},
            {Rva(0xbc09d4), 28}, {Rva(0xbc093a), 10}, {Rva(0xd6ade0), 8}
        };
        unsigned mutations = 0;
        for (const auto& range : ranges) {
            Check(!MinimapMenu10::MatchGuards(image.data(), range.first + range.second - 1, nullptr, currentBuild), "truncated critical guard is rejected");
            for (std::size_t index = 0; index < range.second; ++index) {
                image[range.first + index] ^= 1;
                Check(!MinimapMenu10::MatchGuards(image.data(), image.size(), nullptr, currentBuild), "each changed critical instruction byte is rejected");
                image[range.first + index] ^= 1;
                ++mutations;
            }
        }
        std::uint8_t originalPrefix[18]{};
        std::memcpy(originalPrefix, image.data() + Rva(MinimapMenu10::RendererRva), sizeof(originalPrefix));
        constexpr std::array<std::uint8_t, 18> expectedRendererPrefix = {
            0x4c,0x8b,0xdc,0x55,0x49,0x8d,0xab,0xe8,0xfd,0xff,0xff,0x48,0x81,0xec,0x10,0x03,0x00,0x00};
        Check(std::memcmp(originalPrefix, expectedRendererPrefix.data(), expectedRendererPrefix.size()) == 0,
            "fixture has the exact expected original renderer prefix");
        std::memset(image.data() + Rva(MinimapMenu10::RendererRva), 0xCC, sizeof(originalPrefix));
        Check(!MinimapMenu10::MatchGuards(image.data(), image.size(), nullptr, currentBuild), "raw guards reject a replaced renderer entry");
        Check(MinimapMenu10::MatchGuardsWithRendererPrefix(
            image.data(), image.size(), originalPrefix, sizeof(originalPrefix), nullptr, 0, nullptr, currentBuild),
            "the exact original renderer prefix permits only the owned entry replacement");
        auto wrongPrefix = std::array<std::uint8_t, 18>{};
        std::memcpy(wrongPrefix.data(), originalPrefix, sizeof(originalPrefix));
        wrongPrefix[0] ^= 1;
        Check(!MinimapMenu10::MatchGuardsWithRendererPrefix(
            image.data(), image.size(), wrongPrefix.data(), wrongPrefix.size(), nullptr, 0, nullptr, currentBuild),
            "a mismatched original prefix cannot waive the renderer guard");
        Check(!MinimapMenu10::MatchGuardsWithRendererPrefix(
            image.data(), image.size(), originalPrefix, sizeof(originalPrefix) - 1, nullptr, 0, nullptr, currentBuild),
            "a short original prefix cannot waive the renderer guard");
        image[Rva(0xc4e0d2)] ^= 1;
        Check(!MinimapMenu10::MatchGuardsWithRendererPrefix(
            image.data(), image.size(), originalPrefix, sizeof(originalPrefix), nullptr, 0, nullptr, currentBuild),
            "the renderer tail guard remains required after entry admission");
        image[Rva(0xc4e0d2)] ^= 1;
        image[Rva(0xbc0446)] ^= 1;
        Check(!MinimapMenu10::MatchGuardsWithRendererPrefix(
            image.data(), image.size(), originalPrefix, sizeof(originalPrefix), nullptr, 0, nullptr, currentBuild),
            "a caller guard remains required after entry admission");
        image[Rva(0xbc0446)] ^= 1;
        std::memcpy(image.data() + Rva(MinimapMenu10::RendererRva), originalPrefix, sizeof(originalPrefix));
        Check(MinimapMenu10::MatchGuards(image.data(), image.size(), nullptr, currentBuild), "restored current fixture remains accepted");
        std::cout << "GUARD_MUTATIONS=" << mutations << '\n';
    }
}

int main(int argc, char** argv)
{
    Check(argc == 2, "supply one offline executable fixture");
    CheckSnapshot();
    CheckFixture(argv[1]);
}
