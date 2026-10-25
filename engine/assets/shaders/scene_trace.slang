// Voxel rays across every traced volume through ray queries when the TLAS holds the volumes and a camera centred
// cell grid otherwise and both march the same volumes with voxel_dda
import voxel_dda;
import lighting;

/// Mirrors renderer::VoxelTraceScene in the order of the TLAS custom indices
[[vk::binding(10, 1)]]
StructuredBuffer<VoxelInstance> gTraceVolumes;
/// xyz centre w radius
[[vk::binding(11, 1)]]
StructuredBuffer<float4> gTraceBounds;
/// Mirrors renderer::VoxelTraceScene::writeGrid
[[vk::binding(12, 1)]]
StructuredBuffer<uint> gTraceGrid;

/// Mirrors VoxelTraceScene::k_mask_volume and k_mask_glass
static const uint kTraceMaskVolume = 2u;
static const uint kTraceMaskGlass = 4u;

/// Header words then offset and count per cell
static const uint kTraceVolumeCount = 5u;
static const uint kTraceFarCount = 6u;
static const uint kTraceGlassCount = 7u;
static const uint kTraceFarOffset = 8u;
static const uint kTraceGlassOffset = 9u;
static const uint kTraceEntriesOffset = 10u;
static const uint kTraceHeaderWords = 16u;

/// Ray query candidates may repeat so tinted traversal skips this many recent volumes
static const uint kTraceSeen = 8u;

struct SceneHit
{
    bool hit;
    float t;
    uint volume;
    int3 voxel;
    int axis;
    uint material;
};

bool traceRayQueryEnabled()
{
#if TOAST_RAY_QUERY
    return gCamera.tracedShadowParams.y >= 0.5;
#else
    return false;
#endif
}

/// Cone footprint grows from t 0 so a march restarted mid ray keeps the floor it would have had
RayHit traceMarchVolume(uint v, float3 origin, float3 direction, float tMin, float tMax, float coneSlope, bool tinted,
                        inout float3 transmittance)
{
    const VoxelInstance volume = gTraceVolumes[v];
    const float3 voxelOrigin = mul(volume.worldToVoxel, float4(origin, 1.0)).xyz;
    const float3 voxelDirection = mul(volume.worldToVoxel, float4(direction, 0.0)).xyz;
    const uint record = volume.recordIndex;
    const int3 dims =
        int3(gVolumeRecords[record].brickDimsX, gVolumeRecords[record].brickDimsY, gVolumeRecords[record].brickDimsZ) * int(kBrickDim);

    ConeFloor cone;
    cone.tMin = 0.0;
    cone.slope = coneSlope * length(voxelDirection);
    if (tinted)
    {
        TintGlass tint;
        tint.transmittance = transmittance;
        tint.runEnd = -kFloatMax;
        const RayHit hit = marchCore(record, voxelOrigin, voxelDirection, tMin, tMax, int3(0, 0, 0), dims, true, cone, tint);
        transmittance = tint.transmittance;
        return hit;
    }
    SkipGlass skip;
    return marchCore(record, voxelOrigin, voxelDirection, tMin, tMax, int3(0, 0, 0), dims, true, cone, skip);
}

bool traceBoundsRange(uint v, float3 origin, float3 direction, float tMin, float tMax)
{
    const float4 bounds = gTraceBounds[v];
    const float lengthSquared = max(dot(direction, direction), 1e-12);
    const float along = clamp(dot(bounds.xyz - origin, direction) / lengthSquared, tMin, tMax);
    const float3 nearest = origin + direction * along - bounds.xyz;
    return dot(nearest, nearest) <= bounds.w * bounds.w;
}

bool traceSeen(inout uint seen[kTraceSeen], inout uint seenCount, uint v)
{
    for (uint i = 0u; i < min(seenCount, kTraceSeen); ++i)
    {
        if (seen[i] == v)
        {
            return true;
        }
    }
    seen[seenCount % kTraceSeen] = v;
    ++seenCount;
    return false;
}

/// Grid header unpacked once per ray where a volume is listed in every cell its bounds touch
struct TraceGridWalk
{
    float3 cellOrigin;
    float cellSize;
    int dim;
    uint cellsBase;
    uint entriesBase;
}

TraceGridWalk traceGridWalk()
{
    TraceGridWalk walk;
    walk.cellOrigin = float3(asfloat(gTraceGrid[0]), asfloat(gTraceGrid[1]), asfloat(gTraceGrid[2]));
    walk.cellSize = asfloat(gTraceGrid[3]);
    walk.dim = int(gTraceGrid[4]);
    walk.cellsBase = kTraceHeaderWords;
    walk.entriesBase = gTraceGrid[kTraceEntriesOffset];
    return walk;
}

bool traceGridRange(TraceGridWalk walk, float3 origin, float3 direction, inout float tMin, inout float tMax)
{
    const float3 boxMin = walk.cellOrigin;
    const float3 boxMax = walk.cellOrigin + float(walk.dim) * walk.cellSize;
    const float3 inverse = 1.0 / select(abs(direction) > 1e-12, direction, float3(1e-12, 1e-12, 1e-12));
    const float3 t0 = (boxMin - origin) * inverse;
    const float3 t1 = (boxMax - origin) * inverse;
    const float3 near = min(t0, t1);
    const float3 far = max(t0, t1);
    tMin = max(tMin, max(near.x, max(near.y, near.z)));
    tMax = min(tMax, min(far.x, min(far.y, far.z)));
    return tMin < tMax;
}

/// Order independent visibility so it suits glass tint and any opaque hit ends it
float3 traceVisibilitySoftware(float3 origin, float3 segment, float coneSlope, bool tinted)
{
    float3 transmittance = float3(1.0, 1.0, 1.0);

    const uint farCount = gTraceGrid[kTraceFarCount];
    const uint farBase = gTraceGrid[kTraceFarOffset];
    for (uint f = 0u; f < farCount; ++f)
    {
        const uint v = gTraceGrid[farBase + f];
        if (!traceBoundsRange(v, origin, segment, 0.0, 1.0))
        {
            continue;
        }
        if (traceMarchVolume(v, origin, segment, 0.0, 1.0, coneSlope, tinted, transmittance).hit)
        {
            return float3(0.0, 0.0, 0.0);
        }
    }

    const TraceGridWalk walk = traceGridWalk();
    float tEnter = 0.0;
    float tExit = 1.0;
    if (walk.dim <= 0 || !traceGridRange(walk, origin, segment, tEnter, tExit))
    {
        return transmittance;
    }

    const float3 start = (origin + segment * tEnter - walk.cellOrigin) / walk.cellSize;
    int3 cell = clamp(int3(floor(start)), int3(0, 0, 0), int3(walk.dim - 1, walk.dim - 1, walk.dim - 1));
    const int3 stepSign = int3(sign(segment));
    const float3 cellT = walk.cellSize / max(abs(segment), float3(1e-12, 1e-12, 1e-12));
    float3 nextT;
    [unroll]
    for (int a = 0; a < 3; ++a)
    {
        const float boundary = walk.cellOrigin[a] + (float(cell[a]) + (stepSign[a] > 0 ? 1.0 : 0.0)) * walk.cellSize;
        nextT[a] = stepSign[a] == 0 ? kFloatMax : (boundary - origin[a]) / segment[a];
    }

    float cellEnter = tEnter;
    for (int steps = 0; steps < 3 * walk.dim && cellEnter < tExit; ++steps)
    {
        const float cellExit = min(min(nextT.x, min(nextT.y, nextT.z)), tExit);
        const uint index = uint(cell.x + cell.y * walk.dim + cell.z * walk.dim * walk.dim);
        const uint first = gTraceGrid[walk.cellsBase + 2u * index];
        const uint count = gTraceGrid[walk.cellsBase + 2u * index + 1u];
        for (uint e = 0u; e < count; ++e)
        {
            const uint v = gTraceGrid[walk.entriesBase + first + e];
            if (!traceBoundsRange(v, origin, segment, cellEnter, cellExit))
            {
                continue;
            }
            // Only the part inside this cell so a volume listed in several cells is never counted twice
            if (traceMarchVolume(v, origin, segment, cellEnter, cellExit, coneSlope, tinted, transmittance).hit)
            {
                return float3(0.0, 0.0, 0.0);
            }
        }

        const int axis = nextT.x <= nextT.y ? (nextT.x <= nextT.z ? 0 : 2) : (nextT.y <= nextT.z ? 1 : 2);
        cell[axis] += stepSign[axis];
        if (cell[axis] < 0 || cell[axis] >= walk.dim)
        {
            break;
        }
        nextT[axis] += cellT[axis];
        cellEnter = cellExit;
    }
    return transmittance;
}

/// Transmittance along origin to origin + segment and 0 once a voxel blocks it
float3 traceVisibility(float3 origin, float3 segment, float coneSlope, bool tinted)
{
#if TOAST_RAY_QUERY
    if (traceRayQueryEnabled())
    {
        float3 transmittance = float3(1.0, 1.0, 1.0);
        uint seen[kTraceSeen];
        uint seenCount = 0u;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = segment;
        ray.TMin = 0.0;
        ray.TMax = 1.0;
        RayQuery<RAY_FLAG_NONE> query;
        query.TraceRayInline(gScene, RAY_FLAG_NONE, kTraceMaskVolume, ray);
        while (query.Proceed())
        {
            if (query.CandidateType() != CANDIDATE_PROCEDURAL_PRIMITIVE)
            {
                continue;
            }
            const uint v = query.CandidateInstanceID();
            if (tinted && traceSeen(seen, seenCount, v))
            {
                continue;
            }
            if (traceMarchVolume(v, origin, segment, 0.0, 1.0, coneSlope, tinted, transmittance).hit)
            {
                query.Abort();
                return float3(0.0, 0.0, 0.0);
            }
        }
        return transmittance;
    }
#endif
    return traceVisibilitySoftware(origin, segment, coneSlope, tinted);
}

/// Glass tint only and an opaque voxel ends its own volume but not the ray
float3 traceGlassFilter(float3 origin, float3 segment, float coneSlope)
{
    float3 transmittance = float3(1.0, 1.0, 1.0);
#if TOAST_RAY_QUERY
    if (traceRayQueryEnabled())
    {
        uint seen[kTraceSeen];
        uint seenCount = 0u;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = segment;
        ray.TMin = 0.0;
        ray.TMax = 1.0;
        RayQuery<RAY_FLAG_NONE> query;
        query.TraceRayInline(gScene, RAY_FLAG_NONE, kTraceMaskGlass, ray);
        while (query.Proceed())
        {
            if (query.CandidateType() != CANDIDATE_PROCEDURAL_PRIMITIVE)
            {
                continue;
            }
            const uint v = query.CandidateInstanceID();
            if (traceSeen(seen, seenCount, v))
            {
                continue;
            }
            traceMarchVolume(v, origin, segment, 0.0, 1.0, coneSlope, true, transmittance);
            if (max(transmittance.x, max(transmittance.y, transmittance.z)) <= kTintedOpaque)
            {
                query.Abort();
                return float3(0.0, 0.0, 0.0);
            }
        }
        return transmittance;
    }
#endif
    const uint glassCount = gTraceGrid[kTraceGlassCount];
    const uint glassBase = gTraceGrid[kTraceGlassOffset];
    for (uint g = 0u; g < glassCount; ++g)
    {
        const uint v = gTraceGrid[glassBase + g];
        if (!traceBoundsRange(v, origin, segment, 0.0, 1.0))
        {
            continue;
        }
        traceMarchVolume(v, origin, segment, 0.0, 1.0, coneSlope, true, transmittance);
        if (max(transmittance.x, max(transmittance.y, transmittance.z)) <= kTintedOpaque)
        {
            return float3(0.0, 0.0, 0.0);
        }
    }
    return transmittance;
}

void traceKeepCloser(uint v, RayHit hit, inout SceneHit best)
{
    if (hit.hit && hit.t < best.t)
    {
        best.hit = true;
        best.t = hit.t;
        best.volume = v;
        best.voxel = hit.voxel;
        best.axis = hit.axis;
        best.material = hit.material;
    }
}

/// Closest opaque voxel along origin + direction * t for t in [tMin tMax] while glass passes untinted
SceneHit traceScene(float3 origin, float3 direction, float tMin, float tMax, float coneSlope)
{
    SceneHit best;
    best.hit = false;
    best.t = tMax;
    best.volume = 0u;
    best.voxel = int3(0, 0, 0);
    best.axis = -1;
    best.material = 0u;
    float3 unused = float3(1.0, 1.0, 1.0);

#if TOAST_RAY_QUERY
    if (traceRayQueryEnabled())
    {
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = direction;
        ray.TMin = tMin;
        ray.TMax = tMax;
        RayQuery<RAY_FLAG_NONE> query;
        query.TraceRayInline(gScene, RAY_FLAG_NONE, kTraceMaskVolume, ray);
        while (query.Proceed())
        {
            if (query.CandidateType() != CANDIDATE_PROCEDURAL_PRIMITIVE)
            {
                continue;
            }
            const uint v = query.CandidateInstanceID();
            const RayHit hit = traceMarchVolume(v, origin, direction, tMin, best.t, coneSlope, false, unused);
            if (hit.hit && hit.t < best.t)
            {
                traceKeepCloser(v, hit, best);
                query.CommitProceduralPrimitiveHit(hit.t);
            }
        }
        return best;
    }
#endif

    const uint farCount = gTraceGrid[kTraceFarCount];
    const uint farBase = gTraceGrid[kTraceFarOffset];
    for (uint f = 0u; f < farCount; ++f)
    {
        const uint v = gTraceGrid[farBase + f];
        if (traceBoundsRange(v, origin, direction, tMin, best.t))
        {
            traceKeepCloser(v, traceMarchVolume(v, origin, direction, tMin, best.t, coneSlope, false, unused), best);
        }
    }

    const TraceGridWalk walk = traceGridWalk();
    float tEnter = tMin;
    float tExit = best.t;
    if (walk.dim <= 0 || !traceGridRange(walk, origin, direction, tEnter, tExit))
    {
        return best;
    }

    const float3 start = (origin + direction * tEnter - walk.cellOrigin) / walk.cellSize;
    int3 cell = clamp(int3(floor(start)), int3(0, 0, 0), int3(walk.dim - 1, walk.dim - 1, walk.dim - 1));
    const int3 stepSign = int3(sign(direction));
    const float3 cellT = walk.cellSize / max(abs(direction), float3(1e-12, 1e-12, 1e-12));
    float3 nextT;
    [unroll]
    for (int a = 0; a < 3; ++a)
    {
        const float boundary = walk.cellOrigin[a] + (float(cell[a]) + (stepSign[a] > 0 ? 1.0 : 0.0)) * walk.cellSize;
        nextT[a] = stepSign[a] == 0 ? kFloatMax : (boundary - origin[a]) / direction[a];
    }

    float cellEnter = tEnter;
    for (int steps = 0; steps < 3 * walk.dim && cellEnter < min(tExit, best.t); ++steps)
    {
        const float cellExit = min(min(nextT.x, min(nextT.y, nextT.z)), tExit);
        const uint index = uint(cell.x + cell.y * walk.dim + cell.z * walk.dim * walk.dim);
        const uint first = gTraceGrid[walk.cellsBase + 2u * index];
        const uint count = gTraceGrid[walk.cellsBase + 2u * index + 1u];
        for (uint e = 0u; e < count; ++e)
        {
            const uint v = gTraceGrid[walk.entriesBase + first + e];
            const float limit = min(cellExit, best.t);
            if (traceBoundsRange(v, origin, direction, cellEnter, limit))
            {
                traceKeepCloser(v, traceMarchVolume(v, origin, direction, cellEnter, limit, coneSlope, false, unused), best);
            }
        }

        const int axis = nextT.x <= nextT.y ? (nextT.x <= nextT.z ? 0 : 2) : (nextT.y <= nextT.z ? 1 : 2);
        cell[axis] += stepSign[axis];
        if (cell[axis] < 0 || cell[axis] >= walk.dim)
        {
            break;
        }
        nextT[axis] += cellT[axis];
        cellEnter = cellExit;
    }
    return best;
}

float3 sceneHitNormal(SceneHit hit, float3 direction)
{
    if (!hit.hit || hit.axis < 0)
    {
        return -normalize(direction);
    }
    const float4x4 worldToVoxel = gTraceVolumes[hit.volume].worldToVoxel;
    const float3 voxelDirection = mul(worldToVoxel, float4(direction, 0.0)).xyz;
    float3 voxelNormal = float3(0.0, 0.0, 0.0);
    voxelNormal[hit.axis] = voxelDirection[hit.axis] > 0.0 ? -1.0 : 1.0;
    return normalize(mul(transpose((float3x3)worldToVoxel), voxelNormal));
}
