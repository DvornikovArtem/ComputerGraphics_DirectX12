#define KERNEL_RADIUS 6
#define GROUP_SIZE 16

Texture2D<float> InputTexture : register(t0);
RWTexture2D<float> OutputTexture : register(u0);

static const float Weights[KERNEL_RADIUS + 1] =
{
    0.2143f, 
    0.1805f, 
    0.1218f, 
    0.0652f, 
    0.0284f, 
    0.0101f, 
    0.0029f // % for |x| pixels from center
};

// we'll use group memory it works as:
// thread loads pixels in this cache
// this memory is avalible to all threads
// so if pixel is already there, there's no need to load it again
// this saves a LOT of time
groupshared float Cache[GROUP_SIZE + 2 * KERNEL_RADIUS][GROUP_SIZE + 2 * KERNEL_RADIUS];

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CS_HorizontalBlur(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint2 dimensions;
    InputTexture.GetDimensions(dimensions.x, dimensions.y);
    
    uint2 groupStart = groupID.xy * GROUP_SIZE;
    uint2 globalThreadID = groupStart + groupThreadID.xy;
    
    for (int y = 0; y < (GROUP_SIZE + 2 * KERNEL_RADIUS); y += GROUP_SIZE)
    {
        for (int x = 0; x < (GROUP_SIZE + 2 * KERNEL_RADIUS); x += GROUP_SIZE)
        {
            uint2 loadPos = groupStart + uint2(x, y) - KERNEL_RADIUS + groupThreadID.xy;
            loadPos = clamp(loadPos, uint2(0, 0), dimensions - uint2(1, 1));
            
            int cacheX = groupThreadID.x + x;
            int cacheY = groupThreadID.y + y;
            
            if (cacheX < (GROUP_SIZE + 2 * KERNEL_RADIUS) && cacheY < (GROUP_SIZE + 2 * KERNEL_RADIUS))
            {
                Cache[cacheY][cacheX] = InputTexture[loadPos];
            }
        }
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    if (all(globalThreadID < dimensions))
    {
        float result = 0.0f;
        int centerX = groupThreadID.x + KERNEL_RADIUS;
        int centerY = groupThreadID.y + KERNEL_RADIUS;
        
        result = Cache[centerY][centerX] * Weights[0];
        
        [unroll]
        for (int i = 1; i <= KERNEL_RADIUS; i++)
        {
            float weight = Weights[i];
            result += Cache[centerY][centerX - i] * weight;
            result += Cache[centerY][centerX + i] * weight;
        }
        
        OutputTexture[globalThreadID] = result;
    }
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CS_VerticalBlur(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint2 dimensions;
    InputTexture.GetDimensions(dimensions.x, dimensions.y);
    
    uint2 groupStart = groupID.xy * GROUP_SIZE;
    uint2 globalThreadID = groupStart + groupThreadID.xy;
    
    for (int y = 0; y < (GROUP_SIZE + 2 * KERNEL_RADIUS); y += GROUP_SIZE)
    {
        for (int x = 0; x < (GROUP_SIZE + 2 * KERNEL_RADIUS); x += GROUP_SIZE)
        {
            uint2 loadPos = groupStart + uint2(x, y) - KERNEL_RADIUS + groupThreadID.xy;
            loadPos = clamp(loadPos, uint2(0, 0), dimensions - uint2(1, 1));
            
            int cacheX = groupThreadID.x + x;
            int cacheY = groupThreadID.y + y;
            
            if (cacheX < (GROUP_SIZE + 2 * KERNEL_RADIUS) && cacheY < (GROUP_SIZE + 2 * KERNEL_RADIUS))
            {
                Cache[cacheY][cacheX] = InputTexture[loadPos];
            }
        }
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    if (all(globalThreadID < dimensions))
    {
        float result = 0.0f;
        int centerX = groupThreadID.x + KERNEL_RADIUS;
        int centerY = groupThreadID.y + KERNEL_RADIUS;
        
        result = Cache[centerY][centerX] * Weights[0];
        
        [unroll]
        for (int i = 1; i <= KERNEL_RADIUS; i++)
        {
            float weight = Weights[i];
            result += Cache[centerY - i][centerX] * weight;
            result += Cache[centerY + i][centerX] * weight;
        }
        
        OutputTexture[globalThreadID] = result;
    }
}