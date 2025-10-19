#pragma once
#include <DirectXMath.h>


// Struct that holding noise parameters (frequency, octaves, etc.)
struct NoiseSettings
{
    // Base noise frequency (scales input coordinates)
    float frequency = 0.05f;

    // Number of octaves (how many layers of noise to sum)
    int octaves = 5;

    // Multiplier for frequency per subsequent octave
    float lacunarity = 2.0f;

    // Multiplier for amplitude per subsequent octave
    float gain = 0.5f;

    // Initial amplitude (contribution of the first octave)
    float amplitude = 1.0f;
};


// Declaration of 3D Perlin noise: takes coordinates, returns a value
float Perlin3D(float x, float y, float z);


// Declaration of 3D fBm (fractal Brownian motion) built from Perlin3D and settings
float FBM3D(DirectX::XMFLOAT3 p, const NoiseSettings& s);
