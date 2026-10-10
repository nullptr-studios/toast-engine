// Mirrors renderer/depth_convention.hpp through TOAST_REVERSED_Z
// Shadow maps keep standard depth and do not use these

// The depth buffer clears to it
public static const float kFarDepth = TOAST_REVERSED_Z ? 0.0 : 1.0;

public static const float kNearDepth = TOAST_REVERSED_Z ? 1.0 : 0.0;

// Sky pixels
public bool isFarDepth(float depth)
{
#if TOAST_REVERSED_Z
    return depth <= 0.0;
#else
    return depth >= 1.0;
#endif
}

public bool isCloserDepth(float a, float b)
{
#if TOAST_REVERSED_Z
    return a > b;
#else
    return a < b;
#endif
}

public float closerDepth(float a, float b)
{
#if TOAST_REVERSED_Z
    return max(a, b);
#else
    return min(a, b);
#endif
}

public float fartherDepth(float a, float b)
{
#if TOAST_REVERSED_Z
    return min(a, b);
#else
    return max(a, b);
#endif
}

// Clip space distance from the near plane and >= 0 on or beyond it
public float clipNearDistance(float4 clip)
{
#if TOAST_REVERSED_Z
    return clip.w - clip.z;
#else
    return clip.z;
#endif
}
