#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "../src/client_1_0_profile.h"

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: profile_file_layout_fallback_test <cases.txt>\n";
        return 2;
    }

    const std::filesystem::path casesPath(argv[1]);
    std::ifstream cases(casesPath);
    if (!cases) {
        std::cerr << "cannot open synthetic fixture manifest\n";
        return 2;
    }

    const auto fixtureRoot = casesPath.parent_path();
    std::string line;
    std::size_t count = 0;
    std::size_t failures = 0;
    while (std::getline(cases, line)) {
        if (line.empty()) continue;
        const auto separator = line.find('|');
        if (separator == std::string::npos) {
            std::cerr << "malformed synthetic fixture manifest row\n";
            return 2;
        }

        const auto filename = line.substr(0, separator);
        const auto expectedName = line.substr(separator + 1);
        const auto expected = expectedName == "current"
            ? MinimapClient10::Profile::Build26082781
            : MinimapClient10::Profile::Unsupported;
        const auto actual = MinimapClient10::IdentifyFile(fixtureRoot / filename);
        ++count;
        if (actual != expected) {
            ++failures;
            std::cerr << "FAIL " << filename << " expected=" << expectedName
                      << " actual=" << (actual == MinimapClient10::Profile::Build26082781 ? "current" :
                          actual == MinimapClient10::Profile::Build25988036 ? "legacy" : "unsupported")
                      << '\n';
        } else {
            std::cout << "PASS " << filename << '\n';
        }
    }

    if (!cases.eof()) {
        std::cerr << "error reading synthetic fixture manifest\n";
        return 2;
    }
    std::cout << "profile_file_layout_cases=" << count << " failures=" << failures << '\n';
    return failures == 0 && count != 0 ? 0 : 1;
}
