// NoiseGenerator.h
#pragma once

#ifndef NOISEGENERATOR_H
#define NOISEGENERATOR_H

#include <cstdint>
#include <vector>


namespace Engine::Helpers::NoiseGenerator
{
    struct PerlinParams {
        uint32_t seed = 1337;
        float frequency = 0.002f;
        int   octaves = 5;
        float persistence = 0.5f;
        float lacunarity = 2.0f;
        float offsetX = 0.f;
        float offsetZ = 0.f;
    };

    // Returns [0..65535]
    std::vector<uint16_t> GeneratePerlinHeightMapR16(int width, int height, const PerlinParams& p);
}

#endif // NOISEGENERATOR_H