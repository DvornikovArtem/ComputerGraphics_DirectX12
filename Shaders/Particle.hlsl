// Particle.hlsl


// Define the same Particle structure as in ParticleSystem.h so that
// the expected and actual sizes of the particle data structures match
struct Particle
{
    float3 Pos;
    float LifeTime;
    float3 Vel;
    float Size;
    float4 Color;
};


// Vertex input for the VS (local vertices of the 'quad' or 'circle'): position,
// normal (not used here), UV
struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};


// What the VS outputs to the rasterizer/PS: clip position, color, UV
struct VSOutput
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};


// Frame constant buffer
cbuffer PassConstants : register(b0)
{
    matrix View;
    matrix InvView;
    matrix Proj;
    matrix InvProj;
    matrix ViewProj;
    matrix InvViewProj;
    float3 EyePosW;
    float cbPerObjectPad1;
    float2 RenderTargetSize;
    float2 InvRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;
    float4 AmbientLight;
    float4 FogColor;
    float gFogStart;
    float gFogRange;
    float2 cbPerObjectPad2;
};


// Array of all particles (the entire pool)
StructuredBuffer<Particle> gParticlePool : register(t0);

// Array of indices of 'alive' particles in the pool -> it is used to select which particle the current instance renders
StructuredBuffer<uint> gAliveList : register(t1);


// Vertex shader. Takes a local quad (circle) vertex and the instance ID - the index of the current particle in the alive list
VSOutput VS(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    
    // Get the particle index from the alive list using instanceID
    uint particleIndex = gAliveList[instanceID];
    
    // Read the particle itself from the pool by index
    Particle p = gParticlePool[particleIndex];

    
    // Billboard center in world space
    float3 particlePosW = p.Pos;
    
    // Local 2D position of the quad vertex (e.g., points (-0.5, -0.5), (0.5, -0.5), (-0.5, 0.5), (0.5, 0.5))
    float2 quadPosL = input.PosL.xy;
    
    
    // Take the camera basis from the first and second rows of the inverse view matrix:
    // camRightW - the camera's world right vector
    // camUpW - the camera's world up vector
    // It is a standard technique for constructing a camera-facing billboard)
    float3 camRightW = InvView[0].xyz;
    float3 camUpW = InvView[1].xyz;
    
    
    // Build the world position of the billboard vertex: start from the particle center,
    // then offset along the camera's right/up vectors by the local vertex scaled by the particle size.
    float3 worldPos = particlePosW;
    worldPos += camRightW * quadPosL.x * p.Size;
    worldPos += camUpW * quadPosL.y * p.Size;
    
    
    // Multiply the world position by ViewProj -> get the clip coordinate (needed for rendering to the screen)
    output.PosH = mul(float4(worldPos, 1.0f), ViewProj);
    
    
    // Particle color
    output.Color = p.Color;
    
    // Transparency decreases as LifeTime decreases: divide by 2 seconds (fully opaque threshold = 2.0 and above).
    // Saturate clamps the value to [0,1].
    // Thus, during the last ~2 seconds of its life, the particle smoothly fades out
    output.Color.a *= saturate(p.LifeTime / 2.0f);
    
    // Pass through UVs (in case the PS samples a texture; here the PS doesn't use a texture, but UVs are kept just in case)
    output.TexCoord = input.TexC;

    return output;
}


// Pixel shader
float4 PS(VSOutput input) : SV_TARGET
{
    // Just return color
    return input.Color;
}