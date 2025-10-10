// NoiseGenerator.cpp
#include <Engine/Noise/NoiseGenerator.h>
#include <numeric>
#include <random>
#include <cmath>
#include <algorithm>

namespace Engine::Helpers::NoiseGenerator
{
    namespace {
        struct Perlin {
            std::vector<int> p;
            explicit Perlin(uint32_t seed) {
                p.resize(512);
                std::vector<int> base(256);
                std::iota(base.begin(), base.end(), 0);
                std::mt19937 rng(seed);
                std::shuffle(base.begin(), base.end(), rng);
                for (int i = 0; i < 512; ++i) p[i] = base[i & 255];
            }
            static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
            static float lerp(float t, float a, float b) { return a + t * (b - a); }
            static float grad(int h, float x, float y) {
                int g = h & 3;
                float u = (g < 2) ? x : y, v = (g < 2) ? y : x;
                return ((g & 1) ? -u : u) + ((g & 2) ? -2.0f * v : 2.0f * v) * 0.5f;
            }
            float noise(float x, float y) const {
                int X = int(std::floor(x)) & 255;
                int Y = int(std::floor(y)) & 255;
                x -= std::floor(x); y -= std::floor(y);
                float u = fade(x), v = fade(y);
                int aa = p[p[X] + Y];
                int ab = p[p[X] + Y + 1];
                int ba = p[p[X + 1] + Y];
                int bb = p[p[X + 1] + Y + 1];
                float x1 = lerp(u, grad(aa, x, y), grad(ba, x - 1, y));
                float x2 = lerp(u, grad(ab, x, y - 1), grad(bb, x - 1, y - 1));
                return lerp(v, x1, x2); // ~[-1,1]
            }
        };
    }

    std::vector<uint16_t> GeneratePerlinHeightMapR16(int width, int height, const PerlinParams& params)
    {
        std::vector<uint16_t> out(size_t(width) * size_t(height));
        Perlin perlin(params.seed);

        auto fbm = [&](float fx, float fz)->float {
            float amp = 1.0f, freq = params.frequency;
            float sum = 0.f, norm = 0.f;
            for (int o = 0; o < params.octaves; ++o) {
                sum += amp * perlin.noise(fx * freq + params.offsetX,
                    fz * freq + params.offsetZ);
                norm += amp;
                amp *= params.persistence;
                freq *= params.lacunarity;
            }
            return (norm > 0.f) ? (sum / norm) : 0.f; // [-1...1]
            };

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float n = fbm(float(x), float(y));
                float h01 = 0.5f * n + 0.5f; // [0...1]
                uint16_t hv = (uint16_t)std::round(h01 * 65535.0f);
                out[size_t(y) * size_t(width) + size_t(x)] = hv;
            }
        }
        return out;
    }
}
