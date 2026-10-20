// Small voxel debris called by physics::DebrisSimulation

struct Params {
    float3 gravity;
    float dt;
    float lifetime;
    float restitution;
    float friction;
    float voxelSize;
    uint capacity;
    uint spawnCount;
    uint volumeCount;
    uint substeps;
    uint clear;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
Params gParams;

// most of this structs mirrors my stuff on the cpp

struct DebrisParticle {
    float3 position;
    uint packed;
    float4 rotation;
    float3 velocity;
    uint albedo;
};

struct DebrisMotion {
    float3 angular;
    float age;
    uint restSteps;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct DebrisSpawn {
    float3 position;
    uint slot;
    float4 rotation;
    float3 velocity;
    uint albedo;
    float3 angular;
    uint packed;
};

struct DebrisVolume {
    float4x4 worldToVoxel;
    float4 bounds;
    uint recordIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::binding(0, 0)]]
StructuredBuffer<DebrisParticle> gPrevious;
[[vk::binding(1, 0)]]
RWStructuredBuffer<DebrisParticle> gCurrent;
[[vk::binding(2, 0)]]
RWStructuredBuffer<DebrisMotion> gMotion;
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> gDrawList;
[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> gDrawArgs;
[[vk::binding(5, 0)]]
StructuredBuffer<DebrisSpawn> gSpawns;
[[vk::binding(6, 0)]]
StructuredBuffer<DebrisVolume> gVolumes;

struct VolumeRecord {
    uint brickDimsX;
    uint brickDimsY;
    uint brickDimsZ;
    uint gridOffset;
    uint coarseDimsX;
    uint coarseDimsY;
    uint coarseDimsZ;
    uint coarseOffset;
    uint paletteOffset;
    float maxEmissive;
};

[[vk::binding(0, 1)]]
StructuredBuffer<uint> gBrickMaterials;
[[vk::binding(2, 1)]]
StructuredBuffer<uint> gVolumeGrids;
[[vk::binding(5, 1)]]
StructuredBuffer<VolumeRecord> gVolumeRecords;

static const uint kGroupSize = 64;
static const uint kBrickDim = 8;
static const uint kTagEmpty = 0;
static const uint kTagUniform = 1;

static const uint kMaskBits = 0xFFu;
static const uint kFlagAsleep = 1u << 24;

static const float kSleepSpeed = 0.2;       // below this speed a pice is sleeping
static const uint kSleepSteps = 15;
static const float kBounceSpeed = 0.5;      // slower impact sticks
static const float kContactSlop = 0.001;    // next step does not start touching

uint debrisMaterialAt(uint volume, int3 voxel) {
    VolumeRecord record = gVolumeRecords[volume];
    if (any(voxel < 0) || voxel.x >= int(record.brickDimsX * kBrickDim) || voxel.y >= int(record.brickDimsY * kBrickDim) ||
        voxel.z >= int(record.brickDimsZ * kBrickDim)) {
        return 0u;
    }

    uint3 brick = uint3(voxel) >> 3;
    uint entry = gVolumeGrids[record.gridOffset + brick.x + brick.y * record.brickDimsX + brick.z * (record.brickDimsX * record.brickDimsY)];
    uint tag = entry & 3u;
    if (tag == kTagEmpty) {
        return 0u;
    }

    if (tag == kTagUniform) {
        return entry >> 2;
    }

    uint3 local = uint3(voxel) & 7u;
    uint index = (local.z << 6) | (local.y << 3) | local.x;
    uint word = gBrickMaterials[((entry >> 2) << 7) + (index >> 2)];
    return (word >> ((index & 3u) << 3)) & 0xFFu;
}

float debrisSolid(uint volume, int3 voxel) {
    return debrisMaterialAt(volume, voxel) != 0u ? 1.0 : 0.0;
}

float3 rotateByQuat(float4 q, float3 v) {
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

float4 quatMul(float4 a, float4 b) {
    return float4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}

float3 bitOffset(uint bit) {
    return (float3(float(bit & 1u), float((bit >> 1) & 1u), float(bit >> 2)) - 0.5) * gParams.voxelSize;
}

struct Contact {
    bool hit;
    float3 normal;
    float depth;
};

Contact contactAt(float3 world) {
    // deepest overlap of a point
    Contact contact;
    contact.hit = false;
    contact.normal = float3(0.0, 0.0, 1.0);
    contact.depth = 0.0;

    for (uint v = 0; v < gParams.volumeCount; ++v) {
        DebrisVolume volume = gVolumes[v];
        float3 toCentre = world - volume.bounds.xyz;
        if (dot(toCentre, toCentre) > volume.bounds.w * volume.bounds.w) {
            continue;
        }

        float3 local = mul(volume.worldToVoxel, float4(world, 1.0)).xyz;
        int3 voxel = int3(floor(local));
        if (debrisMaterialAt(volume.recordIndex, voxel) == 0u) {
            continue;
        }

        // Points from solid to empty neighbours
        // holy first time using a gradient outside math class
        float3 gradient = float3(
            debrisSolid(volume.recordIndex, voxel - int3(1, 0, 0)) - debrisSolid(volume.recordIndex, voxel + int3(1, 0, 0)),
            debrisSolid(volume.recordIndex, voxel - int3(0, 1, 0)) - debrisSolid(volume.recordIndex, voxel + int3(0, 1, 0)),
            debrisSolid(volume.recordIndex, voxel - int3(0, 0, 1)) - debrisSolid(volume.recordIndex, voxel + int3(0, 0, 1)));

        if (all(gradient == 0.0)) {
            // Buried so leave towards world up
            gradient = mul(volume.worldToVoxel, float4(0.0, 0.0, 1.0, 0.0)).xyz;
        }

        float3 magnitude = abs(gradient);
        uint axis = magnitude.x >= magnitude.y ? (magnitude.x >= magnitude.z ? 0u : 2u) : (magnitude.y >= magnitude.z ? 1u : 2u);
        float side = gradient[axis] >= 0.0 ? 1.0 : -1.0;
        float inside = frac(local[axis]);
        float depthVoxels = side > 0.0 ? 1.0 - inside : inside;

        float3 voxelNormal = float3(0.0, 0.0, 0.0);
        voxelNormal[axis] = side;
        // same normal transform as voxel.slang shadeHit
        float3 normal = normalize(mul(transpose((float3x3)volume.worldToVoxel), voxelNormal));
        float depth = depthVoxels * gParams.voxelSize;

        if (!contact.hit || depth > contact.depth) {
            contact.hit = true;
            contact.normal = normal;
            contact.depth = depth;
        }
    }
    return contact;
}

void appendVoxels(uint index, uint mask) {
    uint count = countbits(mask);
    if (count == 0u) {
        return;
    }
    uint base = 0u;
    InterlockedAdd(gDrawArgs[1], count, base);
    uint listCapacity = gParams.capacity * 8u;
    uint written = 0u;
    for (uint bit = 0u; bit < 8u; ++bit) {
        if (((mask >> bit) & 1u) != 0u && base + written < listCapacity) {
            gDrawList[base + written] = (index << 3) | bit;
            ++written;
        }
    }
}

[shader("compute")]
[numthreads(1, 1, 1)]
void resetMain(uint3 id : SV_DispatchThreadID) {
    gDrawArgs[0] = 36u;
    gDrawArgs[1] = 0u;
    gDrawArgs[2] = 0u;
    gDrawArgs[3] = 0u;
}

[shader("compute")]
[numthreads(kGroupSize, 1, 1)]
void integrateMain(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= gParams.capacity) {
        return;
    }

    // write every particle
    DebrisParticle particle = gPrevious[i];
    uint mask = particle.packed & kMaskBits;
    if (mask == 0u || gParams.clear != 0u) {
        particle.packed = 0u;
        gCurrent[i] = particle;
        return;
    }

    DebrisMotion motion = gMotion[i];
    const float stepTime = gParams.dt * float(gParams.substeps);
    motion.age += stepTime;
    if (motion.age > gParams.lifetime) {
        particle.packed = 0u;
        gCurrent[i] = particle;
        gMotion[i] = motion;
        return;
    }

    if ((particle.packed & kFlagAsleep) != 0u) {
        // wake up when no floor
        bool supported = false;
        for (uint bit = 0u; bit < 8u && !supported; ++bit) {
            if (((mask >> bit) & 1u) != 0u) {
                float3 below = particle.position + rotateByQuat(particle.rotation, bitOffset(bit)) - float3(0.0, 0.0, gParams.voxelSize);
                supported = contactAt(below).hit;
            }
        }

        if (supported) {
            gCurrent[i] = particle;
            gMotion[i] = motion;
            appendVoxels(i, mask);
            return;
        }
        particle.packed &= ~kFlagAsleep;
        motion.restSteps = 0u;
    }

    bool touching = false;
    for (uint s = 0u; s < gParams.substeps; ++s) {
        float dt = gParams.dt;
        particle.velocity += gParams.gravity * dt;
        particle.position += particle.velocity * dt;

        float spin = length(motion.angular);
        if (spin > 1e-5) {
            particle.rotation = normalize(particle.rotation + 0.5 * dt * quatMul(float4(motion.angular, 0.0), particle.rotation));
        }

        // oone response per substep
        Contact deepest;
        deepest.hit = false;
        deepest.normal = float3(0.0, 0.0, 1.0);
        deepest.depth = 0.0;
        for (uint bit = 0u; bit < 8u; ++bit) {
            if (((mask >> bit) & 1u) == 0u) {
                continue;
            }
            Contact contact = contactAt(particle.position + rotateByQuat(particle.rotation, bitOffset(bit)));
            if (contact.hit && (!deepest.hit || contact.depth > deepest.depth)) {
                deepest = contact;
            }
        }

        if (deepest.hit) {
            touching = true;
            particle.position += deepest.normal * (deepest.depth + kContactSlop);

            float approach = dot(particle.velocity, deepest.normal);
            if (approach < 0.0) {
                float3 tangent = particle.velocity - approach * deepest.normal;
                float bounce = -approach > kBounceSpeed ? -approach * gParams.restitution : 0.0;
                particle.velocity = tangent * saturate(1.0 - gParams.friction) + bounce * deepest.normal;
                motion.angular *= saturate(1.0 - gParams.friction);
            }
        }
    }

    if (touching && length(particle.velocity) < kSleepSpeed) {
        ++motion.restSteps;
    } else {
        motion.restSteps = 0u;
    }

    if (motion.restSteps >= kSleepSteps) {
        particle.velocity = float3(0.0, 0.0, 0.0);
        motion.angular = float3(0.0, 0.0, 0.0);
        particle.packed |= kFlagAsleep;
    }

    gCurrent[i] = particle;
    gMotion[i] = motion;
    appendVoxels(i, mask);
}

[shader("compute")]
[numthreads(kGroupSize, 1, 1)]
void spawnMain(uint3 id : SV_DispatchThreadID) {
    uint j = id.x;
    if (j >= gParams.spawnCount) {
        return;
    }

    DebrisSpawn spawn = gSpawns[j];
    if (spawn.slot >= gParams.capacity) {
        return;
    }

    DebrisParticle particle;
    particle.position = spawn.position;
    particle.packed = spawn.packed & ~kFlagAsleep;
    particle.rotation = spawn.rotation;
    particle.velocity = spawn.velocity;
    particle.albedo = spawn.albedo;
    gCurrent[spawn.slot] = particle;

    DebrisMotion motion;
    motion.angular = spawn.angular;
    motion.age = 0.0;
    motion.restSteps = 0u;
    motion.pad0 = 0u;
    motion.pad1 = 0u;
    motion.pad2 = 0u;
    gMotion[spawn.slot] = motion;

    appendVoxels(spawn.slot, spawn.packed & kMaskBits);
}
