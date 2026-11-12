// Set 0 is the engine scene holding the clipmap of the last frame
import lighting;
import gi_sample;
import gi_clip_common;

/// Over pi per giClipFace of the level written and sampled as B10G11R11 through a second view
[[vk::binding(0, 1)]]
[[vk::image_format("r32ui")]]
RWTexture3D<uint> gAmbient[kGiClipFaces];

/// Word 0 counts the faces listMain found and each record from kFaceListHeader on is a global cell xyz and a face
[[vk::binding(1, 1)]]
RWStructuredBuffer<uint> gFaceList;

static const uint kFaceListHeader = 4u;
/// Mirrors GiClipmapPass k_bounce_capacity and faces past it wait for the next round
static const uint kFaceListCapacity = 1u << 17;

/// Mirrors gi_clip_fill GiClipFillPush
struct GiClipBouncePush
{
    /// xyz first global cell w level
    int4 boxMin;
    int4 boxSize;
};

[[vk::push_constant]]
GiClipBouncePush gPush;

bool clipBounceSolid(GiClipFrame frame, uint level, int3 cell)
{
    const int3 texel = giClipWrap(cell, int(frame.size.x));
    return gGiClipOpacity[NonUniformResourceIndex(level)].Load(int4(texel, 0)).w > 0.0;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void listMain(uint3 id : SV_DispatchThreadID)
{
    if (any(int3(id) >= gPush.boxSize.xyz))
    {
        return;
    }
    const GiClipFrame frame = gGiClipFrame[0];
    const uint level = uint(gPush.boxMin.w);
    if (frame.size.z == 0u || level >= frame.size.y)
    {
        return;
    }
    const int resolution = int(frame.size.x);
    const int3 low = int3(frame.levels[level].xyz);
    const int3 global = gPush.boxMin.xyz + int3(id);
    // Box is from this frame origin while the textures still hold the last frame level
    if (any(global < low) || any(global >= low + resolution) || giClipInner(frame, level, global) ||
        !clipBounceSolid(frame, level, global))
    {
        return;
    }
    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        const int3 neighbour = global + giClipFace(face);
        const bool inside = all(neighbour >= low) && all(neighbour < low + resolution);
        if (!inside || clipBounceSolid(frame, level, neighbour))
        {
            continue;
        }
        uint slot;
        InterlockedAdd(gFaceList[0], 1u, slot);
        if (slot < kFaceListCapacity)
        {
            const uint record = kFaceListHeader + slot * 4u;
            gFaceList[record] = uint(global.x);
            gFaceList[record + 1u] = uint(global.y);
            gFaceList[record + 2u] = uint(global.z);
            gFaceList[record + 3u] = face;
        }
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void bounceMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= min(gFaceList[0], kFaceListCapacity))
    {
        return;
    }
    const GiClipFrame frame = gGiClipFrame[0];
    const uint level = uint(gPush.boxMin.w);
    const uint record = kFaceListHeader + id.x * 4u;
    const int3 global = int3(int(gFaceList[record]), int(gFaceList[record + 1u]), int(gFaceList[record + 2u]));
    const uint face = gFaceList[record + 3u];
    const int3 texel = giClipWrap(global, int(frame.size.x));
    const float cellSize = frame.levels[level].w;
    const float3 normal = float3(giClipFace(face));

    float3 irradiance;
    float sky;
    const float turn = float((uint(texel.x) * 5u + uint(texel.y) * 11u + uint(texel.z) * 7u) & 15u) / 16.0;
    if (!sampleGiClipmap(giClipCellCentre(frame, level, global) + normal * (0.5 * cellSize), normal, turn, irradiance, sky))
    {
        irradiance = float3(0.0, 0.0, 0.0);
        sky = 1.0;
    }
    const float3 ambient =
        gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a +
        (gCamera.environmentParams.x >= 0.5 ? gIrradianceMap.SampleLevel(normal, 0.0).rgb : float3(0.0, 0.0, 0.0));
    gAmbient[face][texel] = giClipPackRadiance(irradiance + sky * ambient);
}
