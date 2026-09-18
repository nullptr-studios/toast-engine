struct ClusterParams
{
    float4x4 inverseProjection;
    uint4 clusterDims;         // x,y,z dims, w = maxLightsPerCluster
    float4 screenSizeNearFar;  // x,y screen dims, z near, w far
    uint lightCount;
    float3 _pad0;
};

struct GpuLight
{
    float4 worldPosRange;   // xyz world-space position, w = attenuation range
    float4 viewPosType;     // xyz view-space position (culling only), w = type (0=point,1=spot)
    float4 colorIntensity;  // rgb color, a intensity
    float4 directionPad;    // xyz world-space direction (spot only)
    float4 coneAngles;      // x = cos(outer), y = cos(inner)
};

struct ClusterAABB
{
    float4 minPoint;
    float4 maxPoint;
};

[[vk::binding(0,0)]]
ConstantBuffer<ClusterParams> gParams;

[[vk::binding(1,0)]]
StructuredBuffer<GpuLight> gLights;

[[vk::binding(2,0)]]
RWStructuredBuffer<ClusterAABB> gClusterAABBs;

[[vk::binding(3,0)]]
RWStructuredBuffer<uint> gClusterLightGrid;

[[vk::binding(4,0)]]
RWStructuredBuffer<uint> gLightIndexList;

// Unprojects a pixel-space screen coordinate into a view-space point lying on the ray from the eye
// (view-space origin) through that pixel. Any NDC z value works here - a perspective matrix maps lines
// through the eye to lines through the origin, so the perspective divide always lands on the same
// eye-centered ray regardless of which z is picked. 0.0 matches this engine's zero-to-one Vulkan depth
// convention (near plane), but the choice is otherwise arbitrary
float3 screenToViewRayPoint(float2 screenPixel)
{
    float2 texCoord = screenPixel / gParams.screenSizeNearFar.xy;
    float2 ndc = texCoord * 2.0 - 1.0;
    float4 clip = float4(ndc, 0.0, 1.0);
    float4 view = mul(gParams.inverseProjection, clip);
    return view.xyz / view.w;
}

// Intersects the line through the view-space eye (origin) and B with the plane z = zDistance
float3 lineIntersectionToZPlane(float3 B, float zDistance)
{
    float t = zDistance / B.z;
    return B * t;
}

float sqDistPointAABB(float3 p, float3 aabbMin, float3 aabbMax)
{
    float3 clamped = clamp(p, aabbMin, aabbMax);
    float3 d = p - clamped;
    return dot(d, d);
}

[numthreads(64, 1, 1)]
[shader("compute")]
void clusterBuildMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint clusterIndex = dispatchThreadID.x;
    uint dimX = gParams.clusterDims.x;
    uint dimY = gParams.clusterDims.y;
    uint dimZ = gParams.clusterDims.z;
    uint clusterCount = dimX * dimY * dimZ;
    if (clusterIndex >= clusterCount)
    {
        return;
    }

    uint i = clusterIndex % dimX;
    uint j = (clusterIndex / dimX) % dimY;
    uint k = clusterIndex / (dimX * dimY);

    float2 tileSize = gParams.screenSizeNearFar.xy / float2(dimX, dimY);
    float2 minPointSS = float2(i, j) * tileSize;
    float2 maxPointSS = float2(i + 1, j + 1) * tileSize;

    float3 minPointRay = screenToViewRayPoint(minPointSS);
    float3 maxPointRay = screenToViewRayPoint(maxPointSS);

    float near = gParams.screenSizeNearFar.z;
    float far = gParams.screenSizeNearFar.w;

    // Camera looks down -Z in view space (glm::lookAt, right-handed), so both slice bounds are negative;
    // logarithmic Z-slicing per the standard Doom2016/Olsson-Assarsson formula
    float clusterNearZ = -(near * pow(far / near, float(k) / float(dimZ)));
    float clusterFarZ = -(near * pow(far / near, float(k + 1) / float(dimZ)));

    float3 minPointNear = lineIntersectionToZPlane(minPointRay, clusterNearZ);
    float3 minPointFar = lineIntersectionToZPlane(minPointRay, clusterFarZ);
    float3 maxPointNear = lineIntersectionToZPlane(maxPointRay, clusterNearZ);
    float3 maxPointFar = lineIntersectionToZPlane(maxPointRay, clusterFarZ);

    float3 minAABB = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
    float3 maxAABB = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

    gClusterAABBs[clusterIndex].minPoint = float4(minAABB, 0.0);
    gClusterAABBs[clusterIndex].maxPoint = float4(maxAABB, 0.0);
}

// One thread per cluster, looping serially over all lights - not one-thread-per-light scattering into
// clusters, which would need atomics and reintroduce a "was the counter reset before this frame" bug.
// Fixed-stride-per-cluster indices avoid that entirely, at the cost of preallocated VRAM
[numthreads(64, 1, 1)]
[shader("compute")]
void lightCullMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint clusterIndex = dispatchThreadID.x;
    uint clusterCount = gParams.clusterDims.x * gParams.clusterDims.y * gParams.clusterDims.z;
    if (clusterIndex >= clusterCount)
    {
        return;
    }

    uint maxLightsPerCluster = gParams.clusterDims.w;
    float3 aabbMin = gClusterAABBs[clusterIndex].minPoint.xyz;
    float3 aabbMax = gClusterAABBs[clusterIndex].maxPoint.xyz;

    uint count = 0;
    uint indexBase = clusterIndex * maxLightsPerCluster;

    for (uint lightIndex = 0; lightIndex < gParams.lightCount; ++lightIndex)
    {
        float3 lightViewPos = gLights[lightIndex].viewPosType.xyz;
        float radius = gLights[lightIndex].worldPosRange.w;

        if (sqDistPointAABB(lightViewPos, aabbMin, aabbMax) <= (radius * radius))
        {
            // Guard the write so an overflowing cluster can't corrupt its neighbor's index-list slice
            if (count < maxLightsPerCluster)
            {
                gLightIndexList[indexBase + count] = lightIndex;
            }
            count++;
        }
    }

    // Clamp the stored count too, so the fragment shader can't read past what was actually written
    gClusterLightGrid[clusterIndex] = min(count, maxLightsPerCluster);
}
