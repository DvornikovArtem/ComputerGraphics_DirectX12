#include <Engine/Voxels/Noise.h>
#include <cmath>


// Perlin's fade curve for smooth interpolation (polynomial 6t^5-15t^4+10t^3)
static inline float fade(float t)
{
    return t*t*t*(t*(t*6-15)+10);
}


// Linear interpolation between a and b with parameter t
static inline float lerp1(float a, float b, float t)
{
    return a + t*(b-a);
}


// Computes gradient contribution for hash h and relative coords (x,y,z)
static inline float grad(int h, float x, float y, float z)
{
    // Limit hash to 16 variants (low 4 bits)
    int hh = h & 15;

    // Choose u: x if hh < 8, else y
    float u = (hh < 8) ? (x) : (y);

    // Choose v: y if hh < 4; if hh == 12 or 14 then x; otherwise z
    float v = (hh < 4) ? (y) : ( (hh==12 || hh==14) ? (x) : (z) );

    // Apply signs to u and v using hash bits and sum - standard Perlin gradient
    return ((hh&1)?-u:u) + ((hh&2)?-v:v);
}


// Permutation table doubled (256*2) to avoid modulo when wrapping
static int p[512];

// Lazy-initialization flag for the permutation table
static bool s_init = false;


// Initialize the permutation table once
static void init_perm()
{
    if(s_init) return;

    // Base Perlin permutation table (fixed set of numbers 0–255)
    static const int perm[256] =
    {
        151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
        140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
        247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
        57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
        74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
        60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
        65,25,63,161,1,216,80,73,209,76,132,187,208, 89,18,169,
        200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
        52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
        207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
        119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,
        9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,
        104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
        241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,
        157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,
        93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    // Copy perm into p[0..255]
    for(int i = 0; i < 256; i++) p[i] = perm[i];

    // Duplicate the array: p[256+i] = p[i] to simplify wraparound indexing.
    for(int i = 0; i < 256; i++) p[256+i] = p[i];

    s_init = true;
}


// Implementation of classic 3D Perlin noise
float Perlin3D(float x, float y, float z)
{
    // Ensure the permutation table is ready.
    init_perm();

    // Integer lattice cell coords masked to [0..255]
    int X = (int)floorf(x) & 255;
    int Y = (int)floorf(y) & 255;
    int Z = (int)floorf(z) & 255;

    // Local fractional coordinates inside the cell [0..1]
    x -= floorf(x);
    y -= floorf(y);
    z -= floorf(z);

    // Apply fade curves on each axis for smooth interpolation
    float u=fade(x), v=fade(y), w=fade(z);

    // Compute hashes for the eight cube corners (via cascaded indexing)
    int A = p[X] + Y, AA = p[A] + Z, AB = p[A+1] + Z;
    int B = p[X+1] + Y, BA = p[B] + Z, BB = p[B+1] + Z;
    
    // Interpolate gradient contributions from the 8 cube corners along u, v, w
    float res =
        lerp1(lerp1(lerp1(grad(p[AA  ], x  , y  , z  ),
                          grad(p[BA  ], x-1, y  , z  ), u),
                    lerp1(grad(p[AB  ], x  , y-1, z  ),
                          grad(p[BB  ], x-1, y-1, z  ), u), v),
              lerp1(lerp1(grad(p[AA+1], x  , y  , z-1),
                          grad(p[BA+1], x-1, y  , z-1), u),
                    lerp1(grad(p[AB+1], x  , y-1, z-1),
                          grad(p[BB+1], x-1, y-1, z-1), u), v), w);

    return res;
}


// fBm: sum multiple Perlin octaves with varying frequency/amplitude
float FBM3D(DirectX::XMFLOAT3 p, const NoiseSettings& s)
{
    // Current amplitude starts from base
    float a = s.amplitude;
    // Current frequency starts from base
    float f = s.frequency;

    // Accumulator for octave sum
    float sum = 0.0f;

    // For each octave: add noise, then update f and a
    for(int i = 0; i < s.octaves; i++)
    {
        // Sample Perlin at scaled coords and weight by amplitude
        sum += a * Perlin3D(p.x * f, p.y * f, p.z * f);

        // Increase frequency for the next octave
        f *= s.lacunarity;
        // Decrease amplitude for the next octave
        a *= s.gain;
    }

    return sum;
}
