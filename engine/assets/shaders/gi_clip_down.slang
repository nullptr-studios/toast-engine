import gi_clip_common;

[[vk::binding(0, 0)]]
StructuredBuffer<GiClipFrame> gClipFrame;

/// xyz opacity along each axis and w the solid share of the children
[[vk::binding(1, 0)]]
Sampler3D gChildOpacity;

/// Premultiplied face radiance of the children
[[vk::binding(2, 0)]]
Sampler3D gChildRadiance[kGiClipFaces];

[[vk::binding(3, 0)]]
Sampler3D gChildAmbient[kGiClipFaces];

[[vk::binding(4, 0)]]
[[vk::image_format("rgba8")]]
RWTexture3D<float4> gOpacity;

[[vk::binding(5, 0)]]
[[vk::image_format("r32ui")]]
RWTexture3D<uint> gRadiance[kGiClipFaces];

[[vk::binding(6, 0)]]
[[vk::image_format("r32ui")]]
RWTexture3D<uint> gAmbient[kGiClipFaces];

/// Mirrors GiClipmapPass DownPush
struct GiClipDownPush
{
    /// xyz first global cell written w cells a side of the texture written
    int4 boxMin;
    /// xyz cells written w cells a side of the texture read
    int4 boxSize;
    /// x mip of the texture read
    int4 mip;
};

[[vk::push_constant]]
GiClipDownPush gPush;

struct ClipChildren
{
    /// Texel and mip of child i which sits at bit 0 x bit 1 y bit 2 z
    int4 at[8];
    float4 opacity[8];
};

ClipChildren clipDown(int3 parent, int3 texel)
{
    ClipChildren children;
    float solid = 0.0;
    for (uint i = 0u; i < 8u; ++i)
    {
        const int3 cell = parent * 2 + int3(i & 1u, (i >> 1) & 1u, i >> 2);
        children.at[i] = int4(giClipWrap(cell, gPush.boxSize.w), gPush.mip.x);
        children.opacity[i] = gChildOpacity.Load(children.at[i]);
        solid += children.opacity[i].w;
    }

    float3 blocked = float3(0.0, 0.0, 0.0);
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        const uint stride = 1u << axis;
        for (uint i = 0u; i < 8u; ++i)
        {
            if ((i & stride) == 0u)
            {
                const float near = children.opacity[i][axis];
                blocked[axis] += near + (1.0 - near) * children.opacity[i | stride][axis];
            }
        }
    }
    gOpacity[texel] = float4(blocked * 0.25, solid * 0.125);

    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        const uint axis = face >> 1;
        const uint stride = 1u << axis;
        const bool positive = (face & 1u) == 0u;
        float3 radiance = float3(0.0, 0.0, 0.0);
        for (uint i = 0u; i < 8u; ++i)
        {
            if ((i & stride) != 0u)
            {
                continue;
            }
            const uint front = positive ? (i | stride) : i;
            const uint back = positive ? i : (i | stride);
            radiance += gChildRadiance[face].Load(children.at[front]).rgb +
                        (1.0 - children.opacity[front][axis]) * gChildRadiance[face].Load(children.at[back]).rgb;
        }
        gRadiance[face][texel] = giClipPackRadiance(radiance * 0.25);
    }
    return children;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void downMain(uint3 id : SV_DispatchThreadID)
{
    if (any(int3(id) >= gPush.boxSize.xyz))
    {
        return;
    }
    const int3 parent = gPush.boxMin.xyz + int3(id);
    const int3 texel = giClipWrap(parent, gPush.boxMin.w);
    const ClipChildren children = clipDown(parent, texel);
    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        float3 ambient = float3(0.0, 0.0, 0.0);
        for (uint i = 0u; i < 8u; ++i)
        {
            ambient += gChildAmbient[face].Load(children.at[i]).rgb;
        }
        // Averaged like the solid share so dividing gives the face irradiance
        gAmbient[face][texel] = giClipPackRadiance(ambient * 0.125);
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void mipMain(uint3 id : SV_DispatchThreadID)
{
    if (any(int3(id) >= gPush.boxSize.xyz))
    {
        return;
    }
    const int3 parent = gPush.boxMin.xyz + int3(id);
    clipDown(parent, giClipWrap(parent, gPush.boxMin.w));
}
