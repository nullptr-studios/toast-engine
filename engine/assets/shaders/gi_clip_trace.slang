import gi_clip_common;

/// Every level bound and unused slots repeat level 0
[[vk::binding(1, 1)]]
Sampler3D gClipMaterial[kGiClipMaxLevels];

bool giClipSolid(GiClipFrame frame, uint level, float3 world)
{
    const int3 cell = int3(floor(world / frame.levels[level].w));
    return gClipMaterial[level].Load(int4(giClipWrap(cell, int(frame.size.x)), 0)).a > 0.0;
}

/// 1 when nothing solid lies between origin and origin + segment and the caller offsets origin off its own cell
float giClipVisibility(GiClipFrame frame, float3 origin, float3 segment)
{
    const float reach = length(segment);
    if (reach <= 0.0)
    {
        return 1.0;
    }
    const float3 direction = segment / reach;
    float t = 0.0;
    for (uint step = 0u; step < 512u && t < reach; ++step)
    {
        const float3 point = origin + direction * t;
        const uint level = giClipLevelAt(frame, point);
        if (level >= kGiClipMaxLevels)
        {
            return 1.0;
        }
        if (giClipSolid(frame, level, point))
        {
            return 0.0;
        }
        t += 0.75 * frame.levels[level].w;
    }
    return 1.0;
}
