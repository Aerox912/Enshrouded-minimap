#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minimap {
// Low-frequency color only. No reference heights, contours or feature pixels
// are used. Brightness and alpha come from the original map at the same texel;
// replacing chroma and rounding can reduce small tonal/color-only differences.
constexpr int ColorPaletteSize = 32;
constexpr int ColorPaletteBytes = ColorPaletteSize * ColorPaletteSize * 3;
inline bool BuildBrightMapPixels(const std::vector<std::uint8_t>& source, int width,
    const std::vector<std::uint8_t>& palette, std::vector<std::uint8_t>& output)
{
    output.clear();
    if (width < 2 || width > 4096 || source.size() != std::size_t(width)*width*4 ||
        palette.size() != ColorPaletteBytes) return false;
    output.resize(source.size());
    constexpr float weights[] = {0.2126f, 0.7152f, 0.0722f};
    for (int y=0; y<width; ++y) for (int x=0; x<width; ++x) {
        const float px=float(x)*(ColorPaletteSize-1)/(width-1);
        const float py=float(y)*(ColorPaletteSize-1)/(width-1);
        const int x0=int(px), y0=int(py), x1=(std::min)(x0+1,ColorPaletteSize-1), y1=(std::min)(y0+1,ColorPaletteSize-1);
        const float tx=px-x0, ty=py-y0;
        const auto at=[&](int a,int b,int c){return palette[(b*ColorPaletteSize+a)*3+c]/255.0f;};
        const auto offset=(std::size_t(y)*width+x)*4;
        float tint[3], luma=0, tintLuma=0;
        for(int c=0;c<3;++c) {
            tint[c]=(at(x0,y0,c)*(1-tx)+at(x1,y0,c)*tx)*(1-ty)+(at(x0,y1,c)*(1-tx)+at(x1,y1,c)*tx)*ty;
            tintLuma+=weights[c]*tint[c];
            luma+=weights[c]*source[offset+c]/255.0f;
        }
        const float brightness=std::pow(luma,0.78f);
        float chroma[3], saturation=1.45f;
        for(int c=0;c<3;++c) {
            chroma[c]=tint[c]-tintLuma;
            // Desaturate toward the target luminance to avoid clipping detail.
            if(chroma[c]>0) saturation=(std::min)(saturation,(1-brightness)/chroma[c]);
            if(chroma[c]<0) saturation=(std::min)(saturation,-brightness/chroma[c]);
        }
        for(int c=0;c<3;++c)
            output[offset+c]=static_cast<std::uint8_t>(std::lround((std::max)(0.0f,(std::min)(1.0f,brightness+saturation*chroma[c]))*255));
        output[offset+3]=source[offset+3];
    }
    return true;
}
}
