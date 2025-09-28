// TextureImporter.h
#pragma once

#ifndef TEXTUREIMPORTER_H
#define TEXTUREIMPORTER_H

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <DirectXTex/DirectXTex.h>

namespace IO
{
    struct Image8 {
        std::vector<uint8_t> pixels;
        int width = 0, height = 0, channels = 0;
        bool empty() const { return width <= 0 || height <= 0 || channels <= 0 || pixels.empty(); }
    };

    struct Image16 {
        std::vector<uint16_t> pixels;
        int width = 0, height = 0, channels = 0;
        bool empty() const { return width <= 0 || height <= 0 || channels <= 0 || pixels.empty(); }
    };

    Image8  LoadAnyAs8(const std::wstring& path, int desiredChannels);
    Image16 LoadAnyAs16(const std::wstring& path, int desiredChannels);

    Image8  CropTo8(const Image8& src, int newW, int newH);
    Image16 CropTo16(const Image16& src, int newW, int newH);

    void CopyFirstRow(DirectX::Image& img, const uint8_t* srcRowBytes, int channelsBytes);
    void CopyFirstColumn(DirectX::Image& img, const uint8_t* srcColBytes, int channelsBytes);
    void ExtractLastRow(const DirectX::Image& img, std::vector<uint8_t>& outRowBytes, int channelsBytes);
    void ExtractLastColumn(const DirectX::Image& img, std::vector<uint8_t>& outColBytes, int channelsBytes);

    inline int AlignDown(int x, int m) { return (x / m) * m; }
}

#endif // TEXTUREIMPORTER_H