// World space since glTF ignores the skinned node transform

// renderer::Vertex packs to 60 bytes but std430 gives 80 and SkinVertex 24 vs 32
static const uint kVertexStride = 60;
static const uint kVertexPositionOffset = 0;
static const uint kVertexNormalOffset = 12;
static const uint kVertexUvOffset = 24;
static const uint kVertexTangentOffset = 32;
static const uint kVertexColorOffset = 48;

static const uint kSkinStride = 24;
static const uint kSkinJointsOffset = 0;
static const uint kSkinWeightsOffset = 8;

[[vk::binding(0,0)]]
ByteAddressBuffer gVertices;

[[vk::binding(1,0)]]
ByteAddressBuffer gSkinVertices;

[[vk::binding(2,0)]]
RWByteAddressBuffer gPosedVertices;

[[vk::binding(3,0)]]
StructuredBuffer<float4x4> gJointMatrices;

struct PushConstants
{
    uint vertexCount;
    uint outputBase;
    uint jointOffset;
    uint _pad0;
};

[[vk::push_constant]]
PushConstants gPush;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID)
{
    uint vertex = threadID.x;
    if (vertex >= gPush.vertexCount)
    {
        return;
    }

    uint sourceByte = vertex * kVertexStride;
    uint skinByte = vertex * kSkinStride;

    float3 position = gVertices.Load<float3>(sourceByte + kVertexPositionOffset);
    float3 normal = gVertices.Load<float3>(sourceByte + kVertexNormalOffset);
    float4 tangent = gVertices.Load<float4>(sourceByte + kVertexTangentOffset);

    // Joint indices are uint16 with four packed into two dwords
    uint2 packedJoints = gSkinVertices.Load<uint2>(skinByte + kSkinJointsOffset);
    float4 weights = gSkinVertices.Load<float4>(skinByte + kSkinWeightsOffset);

    uint4 joints = uint4(
        packedJoints.x & 0xFFFF,
        packedJoints.x >> 16,
        packedJoints.y & 0xFFFF,
        packedJoints.y >> 16);

    float4x4 skinMatrix =
        weights.x * gJointMatrices[gPush.jointOffset + joints.x] +
        weights.y * gJointMatrices[gPush.jointOffset + joints.y] +
        weights.z * gJointMatrices[gPush.jointOffset + joints.z] +
        weights.w * gJointMatrices[gPush.jointOffset + joints.w];

    // Upper 3x3 since rigid joint blends carry no non uniform scale
    float3x3 skinNormalMatrix = (float3x3)skinMatrix;

    uint outputByte = (gPush.outputBase + vertex) * kVertexStride;

    gPosedVertices.Store<float3>(outputByte + kVertexPositionOffset, mul(skinMatrix, float4(position, 1.0)).xyz);
    gPosedVertices.Store<float3>(outputByte + kVertexNormalOffset, normalize(mul(skinNormalMatrix, normal)));
    gPosedVertices.Store<float4>(
        outputByte + kVertexTangentOffset,
        float4(normalize(mul(skinNormalMatrix, tangent.xyz)), tangent.w));

    gPosedVertices.Store<float2>(outputByte + kVertexUvOffset, gVertices.Load<float2>(sourceByte + kVertexUvOffset));
    gPosedVertices.Store<float3>(outputByte + kVertexColorOffset, gVertices.Load<float3>(sourceByte + kVertexColorOffset));
}
