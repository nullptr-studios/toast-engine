// No resources so every GI pass can import it
import depth_convention;

static const uint kGiMaxCascades = 6u;
static const uint kGiEmpty = 0xFFFFFFFFu;
static const uint kGiFlagSeen = 1u;

/// Mirrors renderer::GiPass counters with counts per parity then failures then probes pixels touched
static const uint kGiCounterTableFull = 16u;
static const uint kGiCounterPoolFull = 17u;
static const uint kGiCounterSeen = 18u;
static const uint kGiCounterDepositFull = 24u;
static const uint kGiCounterRays = 25u;
static const uint kGiCounterSlots = 26u;
static const uint kGiCounterInvalidated = 27u;
/// Hit points stored for the secondary cache per pool parity then the ones past the buffer
static const uint kGiCounterPoints = 28u;
static const uint kGiCounterPointsDropped = 30u;
static const uint kGiCounterFresh = 31u;

/// Frames an unseen probe keeps its history and tile
static const uint kGiKeepFrames = 16u;

/// LODs coarser than its points
static const uint kGiSecondaryLods = 2u;

static const float kGiMaxHistory = 4096.0;
static const float kGiMinHistory = 1.0 / 64.0;
/// Sixteenths of the history rays of every 64th bucket summed per cascade
static const uint kGiCounterDepth = 32u;

/// Grid stride passes always dispatch this many and loop over the counters
static const uint kGiStrideThreads = 4096u * 64u;

/// Equal area directions per probe are 2N by N with N this times 2 per cascade
static const uint kGiBaseDirections = 4u;
static const uint kGiC0Directions = 2u * kGiBaseDirections * kGiBaseDirections;

/// 6 by 6 octahedral texels inside a one texel border
static const uint kGiTileSize = 8u;
static const uint kGiTileInterior = 6u;
/// Mirrors renderer::GiPass k_atlas_tiles_per_row and k_atlas_rows
static const uint kGiAtlasTilesPerRow = 256u;
static const uint kGiAtlasRows = 128u;
static const float2 kGiAtlasSize = float2(kGiAtlasTilesPerRow * kGiTileSize, kGiAtlasRows * kGiTileSize);

/// x table offset y table mask z pool offset w pool capacity
typedef uint4 GiCascadeLayout;

/// Mirrors renderer::GiPass::FrameData
struct GiFrame
{
    /// Jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    /// xyz camera w probe spacing at LOD 0 cascade 0
    float4 camera;
    /// x Chebyshev distance where LOD 1 starts y LOD overlap share z lifetime in frames w spawn step in pixels
    float4 lod;
    /// x frame y cascades z pool parity written this frame w 0 drops every carried probe
    uint4 frame;
    /// x width y height zw spawn offset inside the step
    uint4 viewport;
    /// x pool entries per parity y table entries of every cascade z deposit probes of every cascade w direction slots
    uint4 pool;
    /// x interval of c0 in units of its probe spacing y sun reach in meters z deposit fixed point scale w cone slope
    float4 rays;
    /// xy R2 jitter z history kept per frame w history left after a change nearby
    float4 jitter;
    /// x hit points per parity y secondary cache on z asfloat share of off screen hits kept w points kept
    uint4 secondary;
    /// x max ray luminance y history floor in rays z keep of that floor
    float4 limits;
    GiCascadeLayout cascades[kGiMaxCascades];
    /// x deposit probe offset y deposit probe capacity w directions per probe
    uint4 deposits[kGiMaxCascades];
};

struct GiPoint
{
    float3 position;
    /// giOctEncode of the hit normal as two unorm16
    uint normal;
};

struct GiProbe
{
    /// xyz cell w LOD
    int4 cell;
    uint lastSeen;
    /// Index in the previous pool or kGiEmpty for a new probe
    uint previous;
    /// kGiEmpty unless a pixel touched the probe this frame
    uint deposit;
    /// First direction slot this frame or kGiEmpty
    uint slots;
};

/// Cell coordinates modulo 512 stay unique inside the few hundred cells one LOD spans around the camera
uint giKey(int3 cell, uint lod)
{
    return (uint(cell.x) & 511u) | ((uint(cell.y) & 511u) << 9) | ((uint(cell.z) & 511u) << 18) | ((lod & 31u) << 27);
}

uint giHash(uint key)
{
    key ^= key >> 16;
    key *= 0x7feb352du;
    key ^= key >> 15;
    key *= 0x846ca68bu;
    key ^= key >> 16;
    return key;
}

float giRandom(inout uint state)
{
    state = giHash(state + 0x9e3779b9u);
    return float(state >> 8) / 16777216.0;
}

float giSpacing(GiFrame frame, uint lod, uint cascade)
{
    return frame.camera.w * float(1u << min(lod + cascade, 30u));
}

int3 giCell(float3 world, float spacing)
{
    return int3(floor(world / spacing));
}

/// Chebyshev so LOD shells follow the grid
uint giLod(GiFrame frame, float3 world, out bool overlap)
{
    const float3 offset = abs(world - frame.camera.xyz);
    const float distance = max(offset.x, max(offset.y, offset.z));
    const float start = frame.lod.x;
    const uint lod = distance <= start ? 0u : min(uint(floor(log2(distance / start))), 29u);
    overlap = distance >= frame.lod.y * start * float(1u << (lod + 1u));
    return lod;
}

float3 giWorldAt(GiFrame frame, float2 uv, float depth)
{
    const float4 world = mul(frame.inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return world.xyz / world.w;
}

bool giIsSky(float depth)
{
    return depth == kFarDepth;
}

/// Share of a c0 cell along the normal since voxel faces lie on cell planes and jittered depth would flip the cell
static const float kGiSurfaceBias = 0.25;

/// Toward the camera where nothing wrote a normal
float3 giSurfaceNormal(float4 encoded, float3 world, float3 camera)
{
    const float length2 = dot(encoded.xyz, encoded.xyz);
    return length2 > 0.25 ? encoded.xyz * rsqrt(length2) : normalize(camera - world);
}

float giIntervalEnd(GiFrame frame, uint lod, uint cascade)
{
    return frame.rays.x * giSpacing(frame, lod, 0u) * float(1u << min(2u * cascade, 30u));
}

/// Equal area map so every direction bucket covers the same solid angle
float2 giEncodeDirection(float3 direction)
{
    const float phi = atan2(direction.y, direction.x);
    return float2(frac(phi / 6.28318531), saturate(direction.z * 0.5 + 0.5));
}

float3 giDecodeDirection(float2 uv)
{
    const float phi = uv.x * 6.28318531;
    const float z = uv.y * 2.0 - 1.0;
    const float r = sqrt(saturate(1.0 - z * z));
    return float3(r * cos(phi), r * sin(phi), z);
}

uint giSide(uint cascade)
{
    return kGiBaseDirections << cascade;
}

uint giMortonSpread(uint value)
{
    value &= 0x0000ffffu;
    value = (value | (value << 8)) & 0x00ff00ffu;
    value = (value | (value << 4)) & 0x0f0f0f0fu;
    value = (value | (value << 2)) & 0x33333333u;
    value = (value | (value << 1)) & 0x55555555u;
    return value;
}

/// Two Morton ordered N by N halves so the four children of a direction are contiguous one cascade up
/// @p uv from giEncodeDirection so a ray split over every cascade encodes once
uint giDirectionIndex(float2 uv, uint cascade)
{
    const uint side = giSide(cascade);
    const uint x = min(uint(uv.x * float(2u * side)), 2u * side - 1u);
    const uint y = min(uint(uv.y * float(side)), side - 1u);
    return (x / side) * side * side + (giMortonSpread(x % side) | (giMortonSpread(y) << 1));
}

uint giMortonCompact(uint value)
{
    value &= 0x55555555u;
    value = (value | (value >> 1)) & 0x33333333u;
    value = (value | (value >> 2)) & 0x0f0f0f0fu;
    value = (value | (value >> 4)) & 0x00ff00ffu;
    value = (value | (value >> 8)) & 0x0000ffffu;
    return value;
}

float3 giDirectionOf(uint index, uint cascade)
{
    const uint side = giSide(cascade);
    const uint half = index / (side * side);
    const uint morton = index % (side * side);
    const uint x = half * side + giMortonCompact(morton);
    const uint y = giMortonCompact(morton >> 1);
    return giDecodeDirection((float2(x, y) + 0.5) / float2(2u * side, side));
}

float2 giRayPoint(GiFrame frame, uint index)
{
    // Fixed point since a float index loses the fraction past a few hundred thousand rays
    const uint2 r2 = uint2(0x80000000u, 0x80000000u) + index * uint2(3242174889u, 2447445414u);
    return frac(float2(r2 >> 8) / 16777216.0 + frame.jitter.xy);
}

int3 giSurfaceCell(GiFrame frame, float3 world, float3 normal, uint lod, uint cascade)
{
    const float3 point = world + normal * (kGiSurfaceBias * giSpacing(frame, lod, 0u));
    return giCell(point, giSpacing(frame, lod, cascade));
}

/// 16 bit transmittance since a small blend moves the mean in steps a byte would round away
struct GiHistory
{
    /// Red and green halves
    uint redGreen;
    /// Blue half then transmittance as unorm16
    uint blueTransmittance;
    float rays;
};

/// 32 bit maths since f32tof16 needs shaderFloat16 and shaderInt16 which the device does not enable
/// Rounds to nearest and flushes under 2^-14 to zero and saturates past the largest half
uint giFloatToHalf(float value)
{
    const uint bits = asuint(value);
    const uint sign = (bits >> 16) & 0x8000u;
    int exponent = int((bits >> 23) & 0xffu) - 112;
    uint mantissa = (bits & 0x7fffffu) + 0x1000u;
    if ((mantissa & 0x800000u) != 0u)
    {
        mantissa = 0u;
        exponent += 1;
    }
    if (exponent <= 0)
    {
        return sign;
    }
    if (exponent >= 31)
    {
        return sign | 0x7bffu;
    }
    return sign | (uint(exponent) << 10) | (mantissa >> 13);
}

float giHalfToFloat(uint half)
{
    const uint sign = (half & 0x8000u) << 16;
    const uint exponent = (half >> 10) & 0x1fu;
    if (exponent == 0u)
    {
        return asfloat(sign);
    }
    return asfloat(sign | ((exponent + 112u) << 23) | ((half & 0x3ffu) << 13));
}

GiHistory giPackHistory(float3 radiance, float transmittance, float rays)
{
    GiHistory history;
    if (rays < kGiMinHistory)
    {
        history.redGreen = 0u;
        history.blueTransmittance = 0u;
        history.rays = 0.0;
        return history;
    }
    history.redGreen = giFloatToHalf(radiance.r) | (giFloatToHalf(radiance.g) << 16);
    history.blueTransmittance = giFloatToHalf(radiance.b) | (uint(saturate(transmittance) * 65535.0 + 0.5) << 16);
    history.rays = min(rays, kGiMaxHistory);
    return history;
}

/// Transparent with @p rays 0 where nothing ever landed so the cascade above fills it in
float4 giHistoryBucket(GiHistory history, out float rays)
{
    rays = history.rays;
    if (rays < kGiMinHistory)
    {
        rays = 0.0;
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float3 radiance = float3(
        giHalfToFloat(history.redGreen & 0xffffu), giHalfToFloat(history.redGreen >> 16), giHalfToFloat(history.blueTransmittance & 0xffffu)
    );
    return float4(radiance, float(history.blueTransmittance >> 16) / 65535.0);
}

int3 giNeighbourOffset(uint corner)
{
    return int3(int(corner & 1u), int((corner >> 1) & 1u), int((corner >> 2) & 1u));
}

float giNeighbourWeight(uint corner, float3 upper)
{
    const int3 offset = giNeighbourOffset(corner);
    return (offset.x != 0 ? upper.x : 1.0 - upper.x) * (offset.y != 0 ? upper.y : 1.0 - upper.y) *
           (offset.z != 0 ? upper.z : 1.0 - upper.z);
}

/// Lowest of the 8 parent cascade probes around a probe centre and the weight of the upper one per axis
void giParentNeighbourhood(int3 cell, out int3 base, out float3 upper)
{
    const int3 odd = cell & 1;
    base = (cell >> 1) - (int3(1, 1, 1) - odd);
    upper = float3(odd.x != 0 ? 0.25 : 0.75, odd.y != 0 ? 0.25 : 0.75, odd.z != 0 ? 0.25 : 0.75);
}

uint giParentCorner(int3 cell)
{
    const int3 even = int3(1, 1, 1) - (cell & 1);
    return uint(even.x) | (uint(even.y) << 1) | (uint(even.z) << 2);
}

float2 giOctEncode(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 p = n.xy;
    if (n.z < 0.0)
    {
        p = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return p * 0.5 + 0.5;
}

float3 giOctDecode(float2 uv)
{
    const float2 f = uv * 2.0 - 1.0;
    float3 n = float3(f, 1.0 - abs(f.x) - abs(f.y));
    const float fold = saturate(-n.z);
    n.x += n.x >= 0.0 ? -fold : fold;
    n.y += n.y >= 0.0 ? -fold : fold;
    return normalize(n);
}

uint2 giTileOrigin(uint tile)
{
    return uint2(tile % kGiAtlasTilesPerRow, tile / kGiAtlasTilesPerRow) * kGiTileSize;
}

/// Texels 1 to 7 are the interior so bilinear taps reach the border at most
float2 giTileUv(uint tile, float3 normal)
{
    const float2 texel = float2(giTileOrigin(tile)) + 1.0 + giOctEncode(normal) * float(kGiTileInterior);
    return texel / kGiAtlasSize;
}

uint giPackNormal(float3 normal)
{
    const uint2 packed = uint2(saturate(giOctEncode(normal)) * 65535.0 + 0.5);
    return packed.x | (packed.y << 16);
}

float3 giUnpackNormal(uint packed)
{
    return giOctDecode(float2(packed & 0xffffu, packed >> 16) / 65535.0);
}

uint giSecondaryLod(GiFrame frame, float3 world)
{
    bool overlap = false;
    return giLod(frame, world, overlap) + kGiSecondaryLods;
}
