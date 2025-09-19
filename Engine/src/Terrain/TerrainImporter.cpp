// TerrainImporter.cpp

#include <cstdio>
#include <cwctype>

// From solution
#include <Engine/Terrain/TerrainImporter.h>
#include <Engine/RHI/DX12/stb_image.h>


using namespace DirectX;



// Converts all characters of the given string to lowercase
static std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}


// Returns the file extension from the given path and converts it to lowercase
static std::wstring ExtLower(const std::wstring& path) {
    return ToLower(std::filesystem::path(path).extension().wstring());
}


// Returns true if the extension of the file we want to parse matches one of the supported extensions
static bool IsSupportedExt(const std::wstring& ext) {
    return (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".dds");
}


// Extracts the file extension and checks whether it is supported for processing
static void EnsureSupportedOrThrow(const std::wstring& path, const wchar_t* kind) {
    auto ext = ExtLower(path);
    if (!IsSupportedExt(ext)) {
        std::wstringstream ss;
        ss << L"Unsupported " << kind << L" file extension '" << ext << L"'. Allowed: .png, .jpg, .jpeg, .dds";
        ThrowError(L"TerrainImporter::BuildTilesFromSource", ss.str());
    }
}



// Common 8-bit image container
struct LoadedImage8 {

    // A byte array containing the raw pixel data of the image.
    // Tightly packed, rowPitch = width * channels
    // (Pitch = the step in bytes from the beginning of one row to the beginning of the next.
    // RowPitch = pitch along the vertical axis (for rows).
    // In LoadedImage8 everything is tightly packed without alignment, so pitch equals simply width * channels)
    std::vector<uint8_t> pixels;

    // Wodth, height, channels of the image
    int width = 0, height = 0, channels = 0;
};



static LoadedImage8 LoadWithStbAs8(const std::wstring& path, int desired_channels);
static LoadedImage8 LoadDDSAs8(const std::wstring& path, int desired_channels);



// Entry point for loading an image from disk
static LoadedImage8 LoadAnyAs8(const std::wstring& path, int desired_channels) {
    EnsureSupportedOrThrow(path, L"image");
    if (ExtLower(path) == L".dds") return LoadDDSAs8(path, desired_channels);
    else return LoadWithStbAs8(path, desired_channels); // .png/.jpg/.jpeg (8 or 16 bpc)
}



// Load png, jpg, jpeg
static LoadedImage8 LoadWithStbAs8(const std::wstring& path, int desired_channels) {

    // Open the file in binary read mode using _wfopen to avoid Unicode path issues on Windows.
    FILE* f = _wfopen(path.c_str(), L"rb");

    if (!f) {
        std::wstringstream ss; ss << L"Can't open file: " << MakeNoWrap(path);
        ThrowError(L"Load Image (stb)", ss.str());
    }


    // Output variables for stb: width, height, and the actual number of channels in the source file
    int width = 0, height = 0, component = 0;

    // Load as 16-bit always (stb will upconvert 8 -> 16 if needed), then downcast to 8-bit
    stbi_us* data16 = stbi_load_from_file_16(f, &width, &height, &component, desired_channels);

    // Close file right after loading
    fclose(f);

    if (!data16) {
        const char* why = stbi_failure_reason();
        std::wstringstream ss;
        ss << L"stb_image failed: " << (why ? std::wstring(why, why + strlen(why)) : L"(unknown)");
        ThrowError(L"Load Image (stb)", ss.str());
    }


    // Determine the final number of channels: if the user requested a specific one (desired_channels != 0), use it.
    // Otherwise, use the value returned by stb in component
    int ch = desired_channels ? desired_channels : component;

    // Prepare the output structure of our 8-bit per channel format and allocate a buffer for tightly packed pixels (row without padding): rowPitch = width * channels
    LoadedImage8 out;
    out.width = width; out.height = height; out.channels = ch;
    out.pixels.resize(size_t(width) * size_t(height) * size_t(ch));


    // Perform per-element downscaling: from each 16-bit value we take the high byte (shift >> 8).
    // Why this is correct: for original 8-bit images stb increases the bit depth by multiplying by 257 (x16 = x8 * 257),
    // and (x8 * 257) >> 8 == x8. For true 16-bit images this is equivalent to simple quantization to 8-bit
    const size_t n = size_t(width) * size_t(height) * size_t(ch);
    for (size_t i = 0; i < n; ++i) out.pixels[i] = uint8_t(data16[i] >> 8);


    // Free the buffer returned by stb and return our 8-bit container
    stbi_image_free(data16);
    return out;
}



// Load DDS via DirectXTex
static LoadedImage8 LoadDDSAs8(const std::wstring& path, int desired_channels) {
    TexMetadata meta{};
    ScratchImage src;
    HRESULT hr = LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, &meta, src);
    if (FAILED(hr)) {
        std::wstringstream ss; ss << L"LoadFromDDSFile failed: " << std::hex << hr;
        ThrowError(L"Load DDS", ss.str());
    }

    const Image* base = src.GetImage(0, 0, 0);
    const bool wantRGBA = (desired_channels == 4);
    DXGI_FORMAT target = wantRGBA ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;

    ScratchImage stage;
    const Image* img = base;
    TexMetadata curMeta = src.GetMetadata();

    // 1) Decompress if needed
    if (IsCompressed(curMeta.format)) {
        ScratchImage decomp;
        hr = Decompress(src.GetImages(), src.GetImageCount(), curMeta, DXGI_FORMAT_UNKNOWN, decomp);
        if (FAILED(hr)) ThrowError(L"Load DDS", L"Decompress failed.");
        img = decomp.GetImage(0, 0, 0);
        curMeta = decomp.GetMetadata();
        stage = std::move(decomp);
    }

    // 2) Convert to R8G8B8A8_UNORM or R8_UNORM
    if (img->format != target) {
        ScratchImage conv;
        bool stageEmpty = (stage.GetImageCount() == 0);
        hr = Convert(stageEmpty ? src.GetImages() : stage.GetImages(),
            stageEmpty ? src.GetImageCount() : stage.GetImageCount(),
            curMeta, target, TEX_FILTER_DEFAULT, 0.0f, conv);
        if (FAILED(hr)) ThrowError(L"Load DDS", L"Convert failed.");
        img = conv.GetImage(0, 0, 0);
        curMeta = conv.GetMetadata();
        stage = std::move(conv);
    }

    LoadedImage8 out;
    out.width = int(img->width);
    out.height = int(img->height);
    out.channels = desired_channels;

    out.pixels.resize(size_t(out.width) * size_t(out.height) * size_t(out.channels));
    for (int y = 0; y < out.height; ++y) {
        const uint8_t* srcRow = img->pixels + size_t(y) * img->rowPitch;
        uint8_t* dstRow = out.pixels.data() + size_t(y) * size_t(out.width) * size_t(out.channels);
        memcpy(dstRow, srcRow, size_t(out.width) * size_t(out.channels));
    }
    return out;
}



// Rounding down x to the nearest multiple of m
static int AlignDown(int x, int m) { return (x / m) * m; }



// Crops the source image src to the size newW x newH, copying only the top-left portion
static LoadedImage8 CropTo(const LoadedImage8& src, int newW, int newH) {

    // "Copy of the input image
    LoadedImage8 out = src;

    // Update the dimensions - width and height are now cropped
    out.width = newW; out.height = newH;

    // Allocate a new pixel buffer for the new dimensions (cropped rectangle) and initialize it with zeros (black color)
    out.pixels.assign(size_t(newW) * size_t(newH) * size_t(src.channels), 0);


    // Copy the top part of the source image row by row
    for (int y = 0; y < newH; ++y) {

        // s — the beginning of row y in the old image (src.width * channels bytes per row)
        const uint8_t* s = src.pixels.data() + size_t(y) * size_t(src.width) * size_t(src.channels);

        // d — the beginning of row y in the new image
        uint8_t* d = out.pixels.data() + size_t(y) * size_t(newW) * size_t(src.channels);

        // memcpy copies only the first newW * channels bytes (the new width)
        memcpy(d, s, size_t(newW) * size_t(src.channels));
    }

    return out;
}



// Load all terrain textures and use them to generate the tile grid, filling in the information about the tiles and the terrain as a whole
bool TerrainImporter::BuildTilesFromSource(
    const std::wstring& diffuse,
    const std::wstring& normal,
    const std::wstring& height,
    uint32_t quadLevels,
    TerrainMeta& outMeta
)
{
    // Load all three images (accept .png/.jpg/.jpeg/.dds; 8-bit or 16-bit for png/jpg)
    auto imgD = LoadAnyAs8(diffuse, 4); // RGBA8
    auto imgN = LoadAnyAs8(normal, 4);  // RGBA8
    auto imgH = LoadAnyAs8(height, 1);  // R8


    // Validate equal resolution
    if (imgD.width != imgN.width || imgD.height != imgN.height || imgD.width != imgH.width || imgD.height != imgH.height)
    {
        ThrowError(L"TerrainImporter::BuildTilesFromSource", L"All source textures must have identical resolution (diffuse/normal/height).");
    }


    // Compute the number by which width/height must be divisible to properly split into a tile grid for the given number of LOD levels
    // 1u — ensures that we are shifting an unsigned number.
    const uint32_t requiredMultiple = (quadLevels > 0) ? (1u << (quadLevels - 1)) : 1u;


    // Round the diffuse map width/height down to the nearest multiple of requiredMultiple
    int newW = AlignDown(imgD.width, requiredMultiple);
    int newH = AlignDown(imgD.height, requiredMultiple);

    // Synchronize the new target resolution across all three maps: take the minimum of their rounded values so that all three become equal and divisible
    newW = (std::min)({ newW, AlignDown(imgN.width,  requiredMultiple), AlignDown(imgH.width,  requiredMultiple) });
    newH = (std::min)({ newH, AlignDown(imgN.height, requiredMultiple), AlignDown(imgH.height, requiredMultiple) });

    if (newW <= 0 || newH <= 0) ThrowError(L"TerrainImporter::BuildTilesFromSource", L"Source images are too small for requested tiles/LODs.");

    // If the size had to be reduced, carefully crop all three images to (newW, newH) (copy the top-left rectangle row by row).
    // This ensures both divisibility and identical dimensions
    if (newW != imgD.width || newH != imgD.height) {
        imgD = CropTo(imgD, newW, newH);
        imgN = CropTo(imgN, newW, newH);
        imgH = CropTo(imgH, newW, newH);
    }


    // Fill global output metadata
    outMeta.worldSizeX = float(imgD.width);
    outMeta.worldSizeZ = float(imgD.height);


    // Calculate the maximum possible number of LOD levels based on the texture's smallest dimension, assuming each next LOD is 2x smaller
    size_t maxLevelsByDim = 1;
    for (uint32_t m = (std::min)(imgD.width, imgD.height); m > 1; m >>= 1) ++maxLevelsByDim;


    // The actual number of QuadTree levels is the minimum of the requested quadLevels and what the texture size actually allows
    const uint32_t finalLodCount = std::min<uint32_t>(quadLevels, (uint32_t)maxLevelsByDim);
    outMeta.quadLevels = finalLodCount;


    // Calculate the number of leaf tiles per axis (2^(finalLodCount-1)) and the size of a single leaf tile in pixels along X/Y
    const uint32_t leafTilesPerAxis = (finalLodCount > 0) ? (1u << (finalLodCount - 1)) : 1u;
    const uint32_t leafPixX = imgD.width / leafTilesPerAxis;
    const uint32_t leafPixY = imgD.height / leafTilesPerAxis;

    std::wstring msg = L"\n\nleafTilesPerAxis = " + std::to_wstring(leafTilesPerAxis) + L"\n\n";
    OutputDebugStringW(msg.c_str());


    // Store the base tile size in pixels (the side of the square)
    outMeta.baseTilePixels = leafPixX;


    // Prepare a directory for future tiles next to the original diffuse map: <diffeseMap>/Tiles
    std::filesystem::path p(diffuse);
    std::filesystem::path tilesRoot = p.parent_path() / L"Tiles";
    std::filesystem::create_directories(tilesRoot);
    outMeta.tilesRootDir = tilesRoot.wstring();


    // Lambda that, given a side dimension dim, calculates how many mip levels can be built (while dim is divisible by 2).
    // Used later when saving DDS (full mips on tiles)
    auto tileFullMipCount = [&](uint32_t dim)->size_t { size_t lvls = 1; while (dim > 1) { dim >>= 1; ++lvls; } return lvls; };


    // Precompute the row pitch in bytes for each image, taking into account the number of channels:
    //  - diffuse/normal: 4 channels -> width * 4
    //  - height: 1 channel -> width * 1
    const size_t rowPitchD = size_t(imgD.width) * 4;
    const size_t rowPitchN = size_t(imgN.width) * 4;
    const size_t rowPitchH = size_t(imgH.width) * 1;



    for (uint32_t L = 0; L < finalLodCount; ++L)
    {
        const auto levelDir = tilesRoot / (L"L" + std::to_wstring(L));
        std::filesystem::create_directories(levelDir / L"diffuse");
        std::filesystem::create_directories(levelDir / L"normal");
        std::filesystem::create_directories(levelDir / L"height");

        const uint32_t tilesX_L = 1u << L;
        const uint32_t tilesY_L = 1u << L;

        // Tile size on this level: base / 2^L
        const uint32_t tilePixX = imgD.width / tilesX_L;
        const uint32_t tilePixY = imgD.height / tilesY_L;

        const size_t tileMipLevels = tileFullMipCount((std::max)(tilePixX, tilePixY));

        const float worldTileX = outMeta.worldSizeX / float(tilesX_L);
        const float worldTileZ = outMeta.worldSizeZ / float(tilesY_L);

        for (uint32_t ty = 0; ty < tilesY_L; ++ty)
            for (uint32_t tx = 0; tx < tilesX_L; ++tx)
            {
                // ---- DIFFUSE (RGBA8) ----
                std::vector<uint8_t> tileD(size_t(tilePixX) * tilePixY * 4);
                for (uint32_t y = 0; y < tilePixY; ++y) {
                    const uint8_t* srcRow = imgD.pixels.data()
                        + (size_t(ty * tilePixY) + y) * rowPitchD
                        + size_t(tx * tilePixX) * 4;
                    uint8_t* dstRow = tileD.data() + size_t(y) * size_t(tilePixX) * 4;
                    memcpy(dstRow, srcRow, size_t(tilePixX) * 4);
                }
                Image dxImgD{ tilePixX, tilePixY, DXGI_FORMAT_R8G8B8A8_UNORM,
                              size_t(tilePixX) * 4, size_t(tilePixX) * 4 * tilePixY, tileD.data() };

                // Resize the texture to the size of the quadtree leaf
                const Image* baseD = &dxImgD;
                ScratchImage resizedD;
                if (tilePixX != leafPixX || tilePixY != leafPixY) {
                    HRESULT hr = Resize(dxImgD, leafPixX, leafPixY, TEX_FILTER_DEFAULT, resizedD);
                    if (FAILED(hr)) ThrowError(L"TerrainImporter::BuildTilesFromSource", L"Resize (diffuse) failed.");
                    baseD = resizedD.GetImage(0, 0, 0);
                }

                // Generate mip-maps
                ScratchImage mipD;
                const size_t leafMipLevels = tileFullMipCount((std::max)(leafPixX, leafPixY));
                GenerateMipMaps(*baseD, TEX_FILTER_DEFAULT, leafMipLevels, mipD);

                const auto fileD = levelDir / L"diffuse" / (L"tile_" + std::to_wstring(tx) + L"_" + std::to_wstring(ty) + L".dds");
                SaveToDDSFile(mipD.GetImages(), mipD.GetImageCount(), mipD.GetMetadata(), DDS_FLAGS_NONE, fileD.c_str());


                // ---- NORMAL (RGBA8) ----
                std::vector<uint8_t> tileN(size_t(tilePixX) * tilePixY * 4);
                for (uint32_t y = 0; y < tilePixY; ++y) {
                    const uint8_t* srcRow = imgN.pixels.data()
                        + (size_t(ty * tilePixY) + y) * rowPitchN
                        + size_t(tx * tilePixX) * 4;
                    uint8_t* dstRow = tileN.data() + size_t(y) * size_t(tilePixX) * 4;
                    memcpy(dstRow, srcRow, size_t(tilePixX) * 4);
                }
                Image dxImgN{ tilePixX, tilePixY, DXGI_FORMAT_R8G8B8A8_UNORM,
                              size_t(tilePixX) * 4, size_t(tilePixX) * 4 * tilePixY, tileN.data() };


                const Image* baseN = &dxImgN;
                ScratchImage resizedN;
                if (tilePixX != leafPixX || tilePixY != leafPixY) {
                    HRESULT hr = Resize(dxImgN, leafPixX, leafPixY, TEX_FILTER_DEFAULT, resizedN);
                    if (FAILED(hr)) ThrowError(L"TerrainImporter::BuildTilesFromSource", L"Resize (normal) failed.");
                    baseN = resizedN.GetImage(0, 0, 0);
                }

                ScratchImage mipN;
                const size_t leafMipLevels2 = tileFullMipCount((std::max)(leafPixX, leafPixY));
                GenerateMipMaps(*baseN, TEX_FILTER_DEFAULT, leafMipLevels2, mipN);

                const auto fileN = levelDir / L"normal" / (L"tile_" + std::to_wstring(tx) + L"_" + std::to_wstring(ty) + L".dds");
                SaveToDDSFile(mipN.GetImages(), mipN.GetImageCount(), mipN.GetMetadata(), DDS_FLAGS_NONE, fileN.c_str());



                // ---- HEIGHT (R8) + min/max ----
                std::vector<uint8_t> tileH(size_t(tilePixX) * tilePixY);
                uint8_t minV = (std::numeric_limits<uint8_t>::max)();
                uint8_t maxV = (std::numeric_limits<uint8_t>::min)();
                for (uint32_t y = 0; y < tilePixY; ++y) {
                    const uint8_t* srcRow = imgH.pixels.data()
                        + (size_t(ty * tilePixY) + y) * rowPitchH
                        + size_t(tx * tilePixX) * 1;
                    uint8_t* dstRow = tileH.data() + size_t(y) * size_t(tilePixX) * 1;
                    memcpy(dstRow, srcRow, size_t(tilePixX) * 1);
                    for (uint32_t x = 0; x < tilePixX; ++x) {
                        uint8_t v = srcRow[x];
                        if (v < minV) minV = v;
                        if (v > maxV) maxV = v;
                    }
                }
                Image dxImgH{ tilePixX, tilePixY, DXGI_FORMAT_R8_UNORM,
                              size_t(tilePixX) * 1, size_t(tilePixX) * 1 * tilePixY, tileH.data() };

                const Image* baseH = &dxImgH;
                ScratchImage resizedH;
                if (tilePixX != leafPixX || tilePixY != leafPixY) {
                    HRESULT hr = Resize(dxImgH, leafPixX, leafPixY, TEX_FILTER_DEFAULT, resizedH);
                    if (FAILED(hr)) ThrowError(L"TerrainImporter::BuildTilesFromSource", L"Resize (height) failed.");
                    baseH = resizedH.GetImage(0, 0, 0);
                }

                ScratchImage mipH;
                const size_t leafMipLevels3 = tileFullMipCount((std::max)(leafPixX, leafPixY));
                GenerateMipMaps(*baseH, TEX_FILTER_DEFAULT, leafMipLevels3, mipH);

                const auto fileH = levelDir / L"height" / (L"tile_" + std::to_wstring(tx) + L"_" + std::to_wstring(ty) + L".dds");
                SaveToDDSFile(mipH.GetImages(), mipH.GetImageCount(), mipH.GetMetadata(), DDS_FLAGS_NONE, fileH.c_str());

                // ---- META ----
                TerrainTileInfo ti{};
                ti.lod = (uint16_t)L;
                ti.ix = tx;
                ti.iy = ty;
                ti.diffusePath = std::filesystem::relative(fileD, tilesRoot).wstring();
                ti.normalPath = std::filesystem::relative(fileN, tilesRoot).wstring();
                ti.heightPath = std::filesystem::relative(fileH, tilesRoot).wstring();
                ti.minH = float(minV) / 255.0f; ti.maxH = float(maxV) / 255.0f;

                const float x0 = float(tx) * worldTileX;
                const float z0 = float(ty) * worldTileZ;
                const float cx = x0 + 0.5f * worldTileX;
                const float cz = z0 + 0.5f * worldTileZ;
                const float minY = ti.minH * outMeta.heightScale;
                const float maxY = ti.maxH * outMeta.heightScale;
                const float cy = 0.5f * (minY + maxY);
                const float ex = 0.5f * worldTileX;
                const float ez = 0.5f * worldTileZ;
                const float ey = 0.5f * (maxY - minY);
                ti.bounds = BoundingBox(XMFLOAT3(cx, cy, cz), XMFLOAT3(ex, ey, ez));

                outMeta.tiles.push_back(std::move(ti));
            }
    }

    return true;
}