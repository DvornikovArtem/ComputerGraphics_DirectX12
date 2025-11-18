#define KERNEL_RADIUS 6
#define GROUP_SIZE 16

Texture2D<float> InputTexture : register(t0);
RWTexture2D<float> OutputTexture : register(u0);

static const float Weights[KERNEL_RADIUS + 1] =
{
    0.2500f,
    0.1800f,
    0.1300f,
    0.0900f,
    0.0600f,
    0.0350f,
    0.0200f // |x|% from center
};

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CS_HorizontalBlur(uint3 id : SV_DispatchThreadID)
{
    uint2 dimensions;
    InputTexture.GetDimensions(dimensions.x, dimensions.y);
    
    if (any(id.xy >= dimensions))
        return;
    
    float result = InputTexture[id.xy] * Weights[0];
    
    [unroll]
    for (int i = 1; i <= KERNEL_RADIUS; i++)
    {
        float weight = Weights[i];
       
        int2 leftPos = id.xy + int2(-i, 0);
        leftPos.x = max(leftPos.x, 0);
        result += InputTexture[leftPos] * weight;
        
        int2 rightPos = id.xy + int2(i, 0);
        rightPos.x = min(rightPos.x, (int) dimensions.x - 1);
        result += InputTexture[rightPos] * weight;
    }
    
    OutputTexture[id.xy] = result;
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CS_VerticalBlur(uint3 id : SV_DispatchThreadID)
{
    uint2 dimensions;
    InputTexture.GetDimensions(dimensions.x, dimensions.y);
    
    if (any(id.xy >= dimensions))
        return;
    
    float result = InputTexture[id.xy] * Weights[0];
    
    [unroll]
    for (int i = 1; i <= KERNEL_RADIUS; i++)
    {
        float weight = Weights[i];
        
        int2 topPos = id.xy + int2(0, -i);
        topPos.y = max(topPos.y, 0);
        result += InputTexture[topPos] * weight;
        
        int2 bottomPos = id.xy + int2(0, i);
        bottomPos.y = min(bottomPos.y, (int) dimensions.y - 1);
        result += InputTexture[bottomPos] * weight;
    }
    
    OutputTexture[id.xy] = result;
}