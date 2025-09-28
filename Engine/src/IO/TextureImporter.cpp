// TextureImporter.cpp
#include <Engine/IO/TextureImporter.h>
#include <Engine/RHI/DX12/stb_image.h>
#include <Engine/Core/DxException.h>
#include <cstdio>
#include <cwctype>
#include <algorithm>
#include <sstream>

using namespace DirectX;

namespace {
    static std::wstring ToLower(std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
        return s;
    }
    static std::wstring ExtLower(const std::wstring& path) {
        return ToLower(std::filesystem::path(path).extension().wstring());
    }
    static bool IsSupportedExt(const std::wstring& ext) {
        return (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".dds");
    }
    static void EnsureSupportedOrThrow(const std::wstring& path, const wchar_t* kind) {
        auto ext = ExtLower(path);
        if (!IsSupportedExt(ext)) {
            std::wstringstream ss;
            ss << L"Unsupported " << kind << L" file extension '" << ext << L"'. Allowed: .png, .jpg, .jpeg, .dds";
            ThrowError(L"TextureImporter", ss.str());
        }
    }
}

namespace IO
{
    static Image8 LoadWithStbAs8(const std::wstring& path, int desiredChannels)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        {
            std::wstringstream ss; ss << L"Can't open file: " << path;
            ThrowError(L"TextureImporter::LoadWithStbAs8", ss.str());
        }
        int w = 0, h = 0, c = 0;
        stbi_us* data16 = stbi_load_from_file_16(f, &w, &h, &c, desiredChannels);
        fclose(f);
        if (!data16) {
            const char* why = stbi_failure_reason();
            std::wstringstream ss; ss << L"stb_image failed: "
                << (why ? std::wstring(why, why + strlen(why)) : L"(unknown)");
            ThrowError(L"TextureImporter::LoadWithStbAs8", ss.str());
        }
        int ch = desiredChannels ? desiredChannels : c;
        Image8 out; out.width = w; out.height = h; out.channels = ch;
        out.pixels.resize(size_t(w) * size_t(h) * size_t(ch));
        const size_t n = out.pixels.size();
        for (size_t i = 0; i < n; ++i) out.pixels[i] = uint8_t(data16[i] >> 8);
        stbi_image_free(data16);
        return out;
    }

    static Image8 LoadDDSAs8(const std::wstring& path, int desiredChannels)
    {
        TexMetadata meta{};
        ScratchImage src;
        HRESULT hr = LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, &meta, src);
        if (FAILED(hr)) {
            std::wstringstream ss; ss << L"LoadFromDDSFile failed: 0x" << std::hex << hr;
            ThrowError(L"TextureImporter::LoadDDSAs8", ss.str());
        }
        const Image* base = src.GetImage(0, 0, 0);
        DXGI_FORMAT target = (desiredChannels == 4) ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;

        ScratchImage stage;
        const Image* img = base;
        TexMetadata curMeta = src.GetMetadata();

        if (IsCompressed(curMeta.format)) {
            ScratchImage decomp;
            hr = Decompress(src.GetImages(), src.GetImageCount(), curMeta, DXGI_FORMAT_UNKNOWN, decomp);
            if (FAILED(hr)) ThrowError(L"TextureImporter::LoadDDSAs8", L"Decompress failed.");
            img = decomp.GetImage(0, 0, 0);
            curMeta = decomp.GetMetadata();
            stage = std::move(decomp);
        }
        if (img->format != target) {
            ScratchImage conv;
            bool stageEmpty = (stage.GetImageCount() == 0);
            hr = Convert(stageEmpty ? src.GetImages() : stage.GetImages(),
                stageEmpty ? src.GetImageCount() : stage.GetImageCount(),
                curMeta, target, TEX_FILTER_DEFAULT, 0.0f, conv);
            if (FAILED(hr)) ThrowError(L"TextureImporter::LoadDDSAs8", L"Convert failed.");
            img = conv.GetImage(0, 0, 0);
            curMeta = conv.GetMetadata();
            stage = std::move(conv);
        }

        Image8 out;
        out.width = int(img->width);
        out.height = int(img->height);
        out.channels = desiredChannels;
        out.pixels.resize(size_t(out.width) * size_t(out.height) * size_t(out.channels));
        for (int y = 0; y < out.height; ++y) {
            const uint8_t* srcRow = img->pixels + size_t(y) * img->rowPitch;
            uint8_t* dstRow = out.pixels.data() + size_t(y) * size_t(out.width) * size_t(out.channels);
            std::memcpy(dstRow, srcRow, size_t(out.width) * size_t(out.channels));
        }
        return out;
    }

    Image8 LoadAnyAs8(const std::wstring& path, int desiredChannels)
    {
        EnsureSupportedOrThrow(path, L"image");
        if (ExtLower(path) == L".dds") return LoadDDSAs8(path, desiredChannels);
        return LoadWithStbAs8(path, desiredChannels);
    }

    Image16 LoadAnyAs16(const std::wstring& path, int desiredChannels)
    {
        EnsureSupportedOrThrow(path, L"image");
        if (ExtLower(path) == L".dds")
        {
            ScratchImage img;
            ThrowIfFailed(LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, nullptr, img));
            ScratchImage conv;
            ThrowIfFailed(Convert(*img.GetImage(0, 0, 0), DXGI_FORMAT_R16_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, conv));
            const Image* im = conv.GetImage(0, 0, 0);
            Image16 out;
            out.width = int(im->width); out.height = int(im->height); out.channels = 1;
            out.pixels.resize(size_t(out.width) * out.height);
            for (int y = 0; y < out.height; ++y) {
                auto* src = reinterpret_cast<const uint16_t*>(im->pixels + y * im->rowPitch);
                std::memcpy(out.pixels.data() + size_t(y) * out.width, src, size_t(out.width) * sizeof(uint16_t));
            }
            return out;
        }
        // stb_16
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        {
            std::wstringstream ss; ss << L"Can't open file: " << path;
            ThrowError(L"TextureImporter::LoadAnyAs16", ss.str());
        }
        int w = 0, h = 0, c = 0;
        stbi_us* data16 = stbi_load_from_file_16(f, &w, &h, &c, desiredChannels);
        fclose(f);
        if (!data16) {
            const char* why = stbi_failure_reason();
            std::wstringstream ss; ss << L"stb_image failed: "
                << (why ? std::wstring(why, why + strlen(why)) : L"(unknown)");
            ThrowError(L"TextureImporter::LoadAnyAs16", ss.str());
        }
        int ch = desiredChannels ? desiredChannels : c;
        Image16 out; out.width = w; out.height = h; out.channels = ch;
        out.pixels.resize(size_t(w) * size_t(h) * size_t(ch));
        std::memcpy(out.pixels.data(), data16, out.pixels.size() * sizeof(uint16_t));
        stbi_image_free(data16);
        return out;
    }

    Image8 CropTo8(const Image8& src, int newW, int newH)
    {
        Image8 out = src;
        out.width = newW; out.height = newH;
        out.pixels.assign(size_t(newW) * size_t(newH) * size_t(src.channels), 0);
        for (int y = 0; y < newH; ++y) {
            const uint8_t* s = src.pixels.data() + size_t(y) * size_t(src.width) * size_t(src.channels);
            uint8_t* d = out.pixels.data() + size_t(y) * size_t(newW) * size_t(src.channels);
            std::memcpy(d, s, size_t(newW) * size_t(src.channels));
        }
        return out;
    }

    Image16 CropTo16(const Image16& src, int newW, int newH)
    {
        Image16 out;
        if (src.width == 0 || src.height == 0 || newW <= 0 || newH <= 0) return out;
        out.width = newW; out.height = newH; out.channels = src.channels ? src.channels : 1;
        out.pixels.assign(size_t(newW) * size_t(newH) * size_t(out.channels), uint16_t(0));
        const int copyW = (std::min)(newW, src.width);
        const int copyH = (std::min)(newH, src.height);
        for (int y = 0; y < copyH; ++y) {
            const uint16_t* s = src.pixels.data() + size_t(y) * size_t(src.width) * size_t(out.channels);
            uint16_t* d = out.pixels.data() + size_t(y) * size_t(newW) * size_t(out.channels);
            std::memcpy(d, s, size_t(copyW) * size_t(out.channels) * sizeof(uint16_t));
        }
        return out;
    }

    void CopyFirstRow(Image& img, const uint8_t* srcRowBytes, int channelsBytes)
    {
        const size_t rowBytes = size_t(img.width) * size_t(channelsBytes);
        std::memcpy(img.pixels, srcRowBytes, rowBytes);
    }

    void CopyFirstColumn(Image& img, const uint8_t* srcColBytes, int channelsBytes)
    {
        const size_t colBytes = size_t(channelsBytes);
        uint8_t* dst = img.pixels;
        for (size_t y = 0; y < img.height; ++y)
        {
            std::memcpy(dst + y * img.rowPitch, srcColBytes + y * colBytes, colBytes);
        }
    }

    void ExtractLastRow(const Image& img, std::vector<uint8_t>& outRowBytes, int channelsBytes)
    {
        const size_t rowBytes = size_t(img.width) * size_t(channelsBytes);
        outRowBytes.resize(rowBytes);
        const uint8_t* src = img.pixels + (img.height - 1) * img.rowPitch;
        std::memcpy(outRowBytes.data(), src, rowBytes);
    }

    void ExtractLastColumn(const Image& img, std::vector<uint8_t>& outColBytes, int channelsBytes)
    {
        outColBytes.resize(size_t(img.height) * size_t(channelsBytes));
        for (size_t y = 0; y < img.height; ++y)
        {
            const uint8_t* src = img.pixels + y * img.rowPitch + (img.width - 1) * size_t(channelsBytes);
            std::memcpy(outColBytes.data() + y * size_t(channelsBytes), src, size_t(channelsBytes));
        }
    }
}
