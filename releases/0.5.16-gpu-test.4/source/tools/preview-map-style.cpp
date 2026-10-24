// Offline preview uses the same color function as the mod. No game needed.
#include "../src/map_appearance.h"
#include <fstream>
#include <iterator>
#include <iostream>
std::vector<std::uint8_t> Read(const char* path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
int main(int argc,char** argv) {
    if(argc!=4) return 2;
    const auto source=Read(argv[1]), palette=Read(argv[2]);
    const int width=int(std::sqrt(double(source.size()/4)));
    std::vector<std::uint8_t> output;
    if(!minimap::BuildBrightMapPixels(source,width,palette,output)) return 3;
    {std::ifstream existing(argv[3]); if(existing) return 4;}
    std::ofstream file(argv[3],std::ios::binary);
    file.write(reinterpret_cast<const char*>(output.data()),output.size());
    std::cout << width << "x" << width << ": color-only preview\n";
    return file.good()?0:5;
}
