// ParticleSortCS.hlsl


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


// Emitter/simulation constants
cbuffer ParticleConstants : register(b0)
{
    float3 gEmitterPos;
    float gDeltaTime;
    uint gNumEmit;
    uint gCurrentDeadList;
    uint gMaxParticles;
    float particleSize;
    float gTime;
    uint gFrameIndex;
    float3 CameraPos;
    float3 CameraDir;
    float4 _pad;
};


// UAV-buffer with all particles (read/write at arbitrary index)
RWStructuredBuffer<Particle> gParticlePool : register(u0);

// Array of alive particle indices. These are the elements sorted by the depth key
RWStructuredBuffer<uint> gAliveList : register(u3);


// groupshared — memory shared among all threads in a single thread group.
// sKeys stores the sort keys (in this case — particle depth converted to uint).
// sVals stores the values — particle indices corresponding to these keys
groupshared uint sKeys[256]; // depth-key
groupshared uint sVals[256]; // particle index


// Returns the negative projection of the vector from the camera to the particle onto the camera's view direction vector
float DepthKey(float3 p)
{
    return -dot(p - CameraPos, CameraDir);
}


// Each compute shader dispatch runs in thread groups.
// The attribute below defines the size of ONE group: 256 threads along X, 1 along Y, 1 along Z.
// So each group contains exactly 256 parallel threads.
// dispatchThreadID is the global thread index across the entire Dispatch (across all groups).
// groupThreadID — the LOCAL thread index within the current group (from 0 to [numthreads-1] along each axis).
// groupIndex — the same local index, but flattened into a single number (0...255)
[numthreads(256, 1, 1)]
void CS(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    // Global index of the element handled by this thread
    uint idx = dispatchThreadID.x;

    // Get the particle index from the global alive list at position idx
    uint pIndex = gAliveList[idx];
    
    // Compute the "depth key" for sorting by particle position.
    // DepthKey returns a float: depth relative to the camera (projection onto CameraDir)
    float key = DepthKey(gParticlePool[pIndex].Pos);

    
    // Store the key and value in the group's fast shared memory (groupshared),
    // in the slot corresponding to the thread's local index (groupIndex).
    // asuint(key) — bitwise representation of a float as uint
    sKeys[groupIndex] = asuint(key);
    sVals[groupIndex] = pIndex;

    // Synchronization barrier: ensures that ALL threads in the group have written sKeys/sVals
    // before starting the shared sorting
    GroupMemoryBarrierWithGroupSync();
    
    
    // Bitonic sort within the group for 256 elements.
    // Outer loop: increase the size of the "bitonic sequence" from 2 up to 256 (<<= 1 is equal to *=2)
    for (uint size = 2; size <= 256; size*=2)
    {
        // For the current 'size', determine the sort direction of the subarray for EACH thread.
        // In a bitonic merge, half of the subsequence is sorted "up", the other half "down".
        uint dir = (groupIndex & (size/2)) != 0;

        // Inner loop: merge stages with decreasing stride.
        // At each stage, threads compare elements in pairs and swap them if needed.
        for (uint stride = size/2; stride > 0; stride/=2)
        {
            // Position of the current thread in the group's local buffer
            uint pos = groupIndex;
            
            // Find the "partner" for comparison using XOR with the current stride
            uint peer = pos ^ stride;

            
            // Read the key/value pair (own and partner's)
            uint keyA = sKeys[pos];
            uint keyB = sKeys[peer];
            uint valA = sVals[pos];
            uint valB = sVals[peer];
            
            bool swap = ((keyA > keyB) == dir);
            if (swap)
            {
                sKeys[pos] = keyB;
                sKeys[peer] = keyA;
                sVals[pos] = valB;
                sVals[peer] = valA;
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
    
    // Write the sorted "value" (particle index) back to the global list at position idx
    gAliveList[idx] = sVals[groupIndex];
}