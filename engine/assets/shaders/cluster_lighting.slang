struct ClusterParams
{
    float4x4 inverseProjection;
    uint4 clusterDims;         // xyz dims w max lights per cluster
    float4 screenSizeNearFar;  // xy screen size z near w far
    uint lightCount;
    float3 _pad0;
};

// Layout and stride must match VulkanRenderer::GpuLight
struct GpuLight
{
    float4 worldPosRange;
    float4 viewPosType;     // w type 0 point 1 spot
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
            if (count < maxLightsPerCluster)
            {
                gLightIndexList[indexBase + count] = lightIndex;
            }
            count++;
        }
    }

    gClusterLightGrid[clusterIndex] = min(count, maxLightsPerCluster);
}
