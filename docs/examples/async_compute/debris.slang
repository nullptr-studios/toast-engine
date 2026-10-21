// Mirrors DebrisSimulation::Params
struct Params
{
    float dt;
    uint count;
};

[[vk::push_constant]]
Params gParams;

// xyz position w radius
[[vk::binding(0, 0)]]
StructuredBuffer<float4> gPrevious;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float4> gCurrent;

// xyz velocity w unused
[[vk::binding(2, 0)]]
RWStructuredBuffer<float4> gVelocities;

static const float3 kGravity = float3(0.0, -9.81, 0.0);

[shader("compute")]
[numthreads(64, 1, 1)]
void integrateMain(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= gParams.count)
    {
        return;
    }

    float4 particle = gPrevious[i];
    float3 velocity = gVelocities[i].xyz + kGravity * gParams.dt;
    particle.xyz += velocity * gParams.dt;

    // Ground plane bounce so the example stays on screen
    if (particle.y < particle.w)
    {
        particle.y = particle.w;
        velocity.y = -velocity.y * 0.4;
        velocity.xz *= 0.8;
    }

    gCurrent[i] = particle;
    gVelocities[i] = float4(velocity, 0.0);
}
