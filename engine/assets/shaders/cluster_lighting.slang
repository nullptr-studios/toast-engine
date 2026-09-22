struct ClusterParams
{
    float4x4 inverseProjection;
    uint4 clusterDims;         // xyz dims w max lights per cluster
    float4 screenSizeNearFar;  // xy screen size z camera near w camera far
    float4 sliceDepth;         // x log range start y log range end z slices per log unit
    uint4 lightCounts;         // x light count
};

// Layout and stride must match VulkanRenderer::GpuLight
struct GpuLight
{
    float4 worldPosRange;
    float4 viewPosType;     // w type 0 point 1 spot
    float4 viewDirection;   // xyz view space spot axis
    float4 colorIntensity;
    float4 directionPad;    // w shadow layer or -1
    float4 coneAngles;      // x cos outer y cos inner z shadow near w shadow far
    float4 shadowAtlas;     // x fraction of the shadow layer rendered
    float4x4 shadowViewProjection;
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

float3 screenToViewRayPoint(float2 screenPixel)
{
    float2 texCoord = screenPixel / gParams.screenSizeNearFar.xy;
    float2 ndc = texCoord * 2.0 - 1.0;
    float4 clip = float4(ndc, 0.0, 1.0);
    float4 view = mul(gParams.inverseProjection, clip);
    return view.xyz / view.w;
}

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

// Slice 0 is stretched down to the camera near plane
float sliceStartDepth(uint slice)
{
    if (slice == 0)
    {
        return gParams.screenSizeNearFar.z;
    }
    return gParams.sliceDepth.x * exp(float(slice) / gParams.sliceDepth.z);
}

float sliceEndDepth(uint slice)
{
    if (slice + 1 == gParams.clusterDims.z)
    {
        return gParams.sliceDepth.y;
    }
    return gParams.sliceDepth.x * exp(float(slice + 1) / gParams.sliceDepth.z);
}

// Infinite cone so the light range is tested separately
bool sphereIntersectsCone(float3 center, float radius, float3 apex, float3 axis, float cosAngle, float sinAngle)
{
    float3 shifted = center - (apex - (radius / sinAngle) * axis);
    float shiftedSq = dot(shifted, shifted);
    float shiftedAxial = dot(axis, shifted);
    if (shiftedAxial <= 0.0 || shiftedAxial * shiftedAxial < shiftedSq * cosAngle * cosAngle)
    {
        return false;
    }

    float3 fromApex = center - apex;
    float apexSq = dot(fromApex, fromApex);
    float behind = -dot(axis, fromApex);
    if (behind > 0.0 && behind * behind >= apexSq * sinAngle * sinAngle)
    {
        return apexSq <= radius * radius;
    }
    return true;
}

static const float kConePadding = 1.001;

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

    float clusterNearZ = -sliceStartDepth(k);
    float clusterFarZ = -sliceEndDepth(k);

    float3 minPointNear = lineIntersectionToZPlane(minPointRay, clusterNearZ);
    float3 minPointFar = lineIntersectionToZPlane(minPointRay, clusterFarZ);
    float3 maxPointNear = lineIntersectionToZPlane(maxPointRay, clusterNearZ);
    float3 maxPointFar = lineIntersectionToZPlane(maxPointRay, clusterFarZ);

    float3 minAABB = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
    float3 maxAABB = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

    gClusterAABBs[clusterIndex].minPoint = float4(minAABB, 0.0);
    gClusterAABBs[clusterIndex].maxPoint = float4(maxAABB, 0.0);
}

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

    float3 aabbCenter = (aabbMin + aabbMax) * 0.5;
    float aabbRadius = length(aabbMax - aabbMin) * 0.5 * kConePadding + 0.0001;

    uint count = 0;
    uint indexBase = clusterIndex * maxLightsPerCluster;

    for (uint lightIndex = 0; lightIndex < gParams.lightCounts.x; ++lightIndex)
    {
        float3 lightViewPos = gLights[lightIndex].viewPosType.xyz;
        float radius = gLights[lightIndex].worldPosRange.w;

        if (sqDistPointAABB(lightViewPos, aabbMin, aabbMax) > (radius * radius))
        {
            continue;
        }

        float cosAngle = gLights[lightIndex].coneAngles.x;
        if (gLights[lightIndex].viewPosType.w > 0.5 && cosAngle > 0.0)
        {
            float sinAngle = max(sqrt(saturate(1.0 - cosAngle * cosAngle)), 0.001);
            if (!sphereIntersectsCone(
                    aabbCenter, aabbRadius, lightViewPos, gLights[lightIndex].viewDirection.xyz, cosAngle, sinAngle))
            {
                continue;
            }
        }

        if (count < maxLightsPerCluster)
        {
            gLightIndexList[indexBase + count] = lightIndex;
        }
        count++;
    }

    // Unclamped so overflow stays visible and readers clamp to the list stride
    gClusterLightGrid[clusterIndex] = count;
}
