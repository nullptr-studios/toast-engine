// Every level of renderer::HiZPyramid in one dispatch where each texel keeps the farthest depth of the 2x2 texels under it
// Groups reduce 32x32 tiles of level 0 to level 5 in shared memory and the last group to finish reduces the rest
import depth_convention;

/// Mirrors renderer::HiZPyramid::kMaxLevels
static const uint kMaxLevels = 13u;
static const uint kTile = 32u;
static const uint kTileLevels = 6u;

/// Mirrors renderer::HiZPyramid::ReducePush
struct ReducePush
{
    uint2 sourceSize;
    uint levelCount;
    uint groupCount;
};

[[vk::push_constant]]
ReducePush gReduce;

[[vk::binding(0, 0)]]
Sampler2D gDepth;

/// Levels past levelCount repeat the last view
[[vk::binding(1, 0)]]
[[vk::image_format("r32f")]]
globallycoherent RWTexture2D<float> gLevels[kMaxLevels];

/// Counts finished groups and the last one resets it
[[vk::binding(2, 0)]]
globallycoherent RWStructuredBuffer<uint> gCounter;

groupshared float sDepth[kTile][kTile];
groupshared bool sLast;

/// Halving rounds up so level k texel t covers level k - 1 texels 2t and 2t + 1 when they exist
uint2 levelSize(uint level)
{
    const uint shift = level + 1u;
    return (gReduce.sourceSize + ((1u << shift) - 1u)) >> shift;
}

/// Missing texels are the near depth which no farther test ever keeps
float sourceDepth(uint2 pixel)
{
    if (pixel.x >= gReduce.sourceSize.x || pixel.y >= gReduce.sourceSize.y)
    {
        return kNearDepth;
    }
    return gDepth.Load(int3(int2(pixel), 0)).x;
}

float levelDepth(uint level, uint2 texel)
{
    const uint2 size = levelSize(level);
    if (texel.x >= size.x || texel.y >= size.y)
    {
        return kNearDepth;
    }
    return gLevels[level][texel];
}

void storeLevel(uint level, uint2 texel, float depth)
{
    const uint2 size = levelSize(level);
    if (texel.x < size.x && texel.y < size.y)
    {
        gLevels[level][texel] = depth;
    }
}

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint index : SV_GroupIndex)
{
    // Level 0 at 2x2 texels per thread
    const uint2 thread = uint2(index & 15u, index >> 4);
    for (uint corner = 0u; corner < 4u; ++corner)
    {
        const uint2 local = thread * 2u + uint2(corner & 1u, corner >> 1);
        const uint2 texel = group.xy * kTile + local;
        const uint2 pixel = texel * 2u;
        float depth = fartherDepth(sourceDepth(pixel), sourceDepth(pixel + uint2(1u, 0u)));
        depth = fartherDepth(depth, fartherDepth(sourceDepth(pixel + uint2(0u, 1u)), sourceDepth(pixel + uint2(1u, 1u))));
        sDepth[local.y][local.x] = depth;
        storeLevel(0u, texel, depth);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint level = 1u; level < kTileLevels && level < gReduce.levelCount; ++level)
    {
        const uint width = kTile >> level;
        const bool active = index < width * width;
        const uint2 local = uint2(index % width, index / width);
        float depth = kNearDepth;
        if (active)
        {
            const uint2 child = local * 2u;
            depth = fartherDepth(fartherDepth(sDepth[child.y][child.x], sDepth[child.y][child.x + 1u]),
                                 fartherDepth(sDepth[child.y + 1u][child.x], sDepth[child.y + 1u][child.x + 1u]));
        }
        GroupMemoryBarrierWithGroupSync();
        if (active)
        {
            sDepth[local.y][local.x] = depth;
            storeLevel(level, group.xy * width + local, depth);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (gReduce.levelCount <= kTileLevels)
    {
        return;
    }

    // Level 5 writes of this group land before the count says it finished
    AllMemoryBarrierWithGroupSync();
    if (index == 0u)
    {
        uint finished;
        InterlockedAdd(gCounter[0], 1u, finished);
        sLast = finished == gReduce.groupCount - 1u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (!sLast)
    {
        return;
    }
    AllMemoryBarrier();

    for (uint level = kTileLevels; level < gReduce.levelCount; ++level)
    {
        const uint2 size = levelSize(level);
        for (uint i = index; i < size.x * size.y; i += 256u)
        {
            const uint2 texel = uint2(i % size.x, i / size.x);
            const uint2 child = texel * 2u;
            const float depth = fartherDepth(fartherDepth(levelDepth(level - 1u, child), levelDepth(level - 1u, child + uint2(1u, 0u))),
                                             fartherDepth(levelDepth(level - 1u, child + uint2(0u, 1u)),
                                                          levelDepth(level - 1u, child + uint2(1u, 1u))));
            gLevels[level][texel] = depth;
        }
        AllMemoryBarrierWithGroupSync();
    }

    if (index == 0u)
    {
        gCounter[0] = 0u;
    }
}
