// Small voxel debris called by physics::DebrisSimulation

struct Params {
    float3 gravity;
    float dt;
    float fadeIn;
    float restitution;
    float friction;
    float voxelSize;
    uint capacity;
    uint spawnCount;
    uint volumeCount;
    uint substeps;
    uint clear;
    uint retireCount;
    float fadeOut;
    uint pad0;
};

[[vk::push_constant]]
Params gParams;

// Mirrors physics::DebrisSimulation Gpu structs

struct DebrisParticle {
    float3 position;
    uint packed;
    float4 rotation;
    float3 velocity;
    uint albedo;
    float scale;
    uint glass;
    uint pad0;
    uint pad1;
};

struct DebrisMotion {
    float3 angular;
    float age;
    uint restSteps;
    float lifetime;
    float baseScale;
    uint pad0;
};

struct DebrisSpawn {
    float3 position;
    uint slot;
    float4 rotation;
    float3 velocity;
    uint albedo;
    float3 angular;
    uint packed;
    float lifetime;
    float scale;
    uint glass;
    uint pad0;
};

struct DebrisVolume {
    float4x4 worldToVoxel;
    float4 bounds;
    float4 boxMin;
    float4 boxMax;
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
[[vk::binding(7, 0)]]
StructuredBuffer<uint> gRetires;

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
    uint glassOffset;
    int textureOriginX;
    int textureOriginY;
    int textureOriginZ;
};

[[vk::binding(0, 1)]]
StructuredBuffer<uint> gBrickMaterials;
[[vk::binding(2, 1)]]
StructuredBuffer<uint> gVolumeGrids;
[[vk::binding(5, 1)]]
StructuredBuffer<VolumeRecord> gVolumeRecords;

static const uint kGroupSize = 64;
static const uint kMaxVolumes = 512;        // mirrors DebrisSimulation::k_max_volumes
static const uint kMaxCandidates = 32;
static const uint kBrickDim = 8;
static const uint kTagEmpty = 0;
static const uint kTagUniform = 1;

static const uint kMaskBits = 0xFFu;
static const uint kFlagAsleep = 1u << 24;
static const uint kFlagGlass = 1u << 25;
static const float kSpawnScale = 0.4;       // grows from this share of its size

static const float kSleepSpeed = 0.2;       // below this speed a piece may sleep
static const uint kSleepSteps = 15;
static const float kBounceSpeed = 0.5;      // slower impact sticks
static const float kContactSlop = 0.001;    // next step does not start touching
static const float kFloorZ = 0.0f;          // lowest height a piece reaches
static const uint kMaxSweeps = 8;
static const float kSleepSpin = 1.0;
static const float kMaxSpin = 30.0;

// Graphics queue patches sections in place unordered with this queue so new records can still be uninitialized headroom
// No robust buffer access so every index is checked against the buffer
uint debrisMaterialAt(uint volume, int3 voxel) {
    uint count = 0u;
    uint stride = 0u;
    gVolumeRecords.GetDimensions(count, stride);
    if (volume >= count) {
        return 0u;
    }

    VolumeRecord record = gVolumeRecords[volume];
    if (any(voxel < 0) || voxel.x >= int(record.brickDimsX * kBrickDim) || voxel.y >= int(record.brickDimsY * kBrickDim) ||
        voxel.z >= int(record.brickDimsZ * kBrickDim)) {
        return 0u;
    }

    uint3 brick = uint3(voxel) >> 3;
    uint gridIndex = record.gridOffset + brick.x + brick.y * record.brickDimsX + brick.z * (record.brickDimsX * record.brickDimsY);
    gVolumeGrids.GetDimensions(count, stride);
    if (gridIndex >= count) {
        return 0u;
    }
    uint entry = gVolumeGrids[gridIndex];
    uint tag = entry & 3u;
    if (tag == kTagEmpty) {
        return 0u;
    }

    if (tag == kTagUniform) {
        return entry >> 2;
    }

    uint3 local = uint3(voxel) & 7u;
    uint index = (local.z << 6) | (local.y << 3) | local.x;
    uint materialIndex = ((entry >> 2) << 7) + (index >> 2);
    gBrickMaterials.GetDimensions(count, stride);
    if (materialIndex >= count) {
        return 0u;
    }
    uint word = gBrickMaterials[materialIndex];
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

float3 bitOffset(uint bit, float scale) {
    return (float3(float(bit & 1u), float((bit >> 1) & 1u), float(bit >> 2)) - 0.5) * (gParams.voxelSize * scale);
}

// look if touches a cube that is present
bool latticePresent(uint mask, int3 p) {
    uint3 low = uint3(p.x == 1 ? 1 : 0, p.y == 1 ? 1 : 0, p.z == 1 ? 1 : 0);
    uint3 high = uint3(p.x == -1 ? 0 : 1, p.y == -1 ? 0 : 1, p.z == -1 ? 0 : 1);
    for (uint z = low.z; z <= high.z; ++z) {
        for (uint y = low.y; y <= high.y; ++y) {
            for (uint x = low.x; x <= high.x; ++x) {
                if (((mask >> (x + 2u * y + 4u * z)) & 1u) != 0u) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Grows in after spawning and shrinks away before dying
float displayScale(DebrisMotion motion) {
    float grow = lerp(kSpawnScale, 1.0, saturate(motion.age / max(gParams.fadeIn, 1e-4)));
    float shrink = smoothstep(0.0, max(gParams.fadeOut, 1e-4), motion.lifetime - motion.age);
    return motion.baseScale * grow * shrink;
}

struct Contact {
    bool hit;
    float3 normal;
    float depth;
};

groupshared float4 sBoxMin[kMaxVolumes];
groupshared float4 sBoxMax[kMaxVolumes];

struct Candidates {
    uint count;
    uint index[kMaxCandidates];
};

// Volumes whose bounds meet the sphere the piece sweeps this step so each point skips the rest
Candidates gatherCandidates(float3 centre, float radius) {
    Candidates candidates;
    candidates.count = 0u;
    uint volumeCount = min(gParams.volumeCount, kMaxVolumes);
    for (uint v = 0u; v < volumeCount && candidates.count < kMaxCandidates; ++v) {
        if (all(centre >= sBoxMin[v].xyz - radius) && all(centre <= sBoxMax[v].xyz + radius)) {
            candidates.index[candidates.count] = v;
            ++candidates.count;
        }
    }
    return candidates;
}

Contact contactAt(float3 world, Candidates candidates) {
    // Deepest overlap of a point
    Contact contact;
    contact.hit = false;
    contact.normal = float3(0.0, 0.0, 1.0);
    contact.depth = 0.0;

    for (uint c = 0u; c < candidates.count; ++c) {
        uint v = candidates.index[c];
        if (any(world < sBoxMin[v].xyz) || any(world > sBoxMax[v].xyz)) {
            continue;
        }

        DebrisVolume volume = gVolumes[v];
        float3 local = mul(volume.worldToVoxel, float4(world, 1.0)).xyz;
        int3 voxel = int3(floor(local));
        if (debrisMaterialAt(volume.recordIndex, voxel) == 0u) {
            continue;
        }

        // Leaves through the nearest face
        uint axis = 2u;
        float side = 1.0;
        float depthVoxels = 1e9;
        for (uint a = 0u; a < 3u; ++a) {
            int3 step = int3(0, 0, 0);
            step[a] = 1;
            float inside = frac(local[a]);
            if (debrisSolid(volume.recordIndex, voxel - step) == 0.0 && inside < depthVoxels) {
                depthVoxels = inside;
                axis = a;
                side = -1.0;
            }
            if (debrisSolid(volume.recordIndex, voxel + step) == 0.0 && 1.0 - inside < depthVoxels) {
                depthVoxels = 1.0 - inside;
                axis = a;
                side = 1.0;
            }
        }

        if (depthVoxels > 1e8) {
            // Buried so leave towards world up
            float3 up = mul(volume.worldToVoxel, float4(0.0, 0.0, 1.0, 0.0)).xyz;
            float3 magnitude = abs(up);
            axis = magnitude.x >= magnitude.y ? (magnitude.x >= magnitude.z ? 0u : 2u) : (magnitude.y >= magnitude.z ? 1u : 2u);
            side = up[axis] >= 0.0 ? 1.0 : -1.0;
            float inside = frac(local[axis]);
            depthVoxels = side > 0.0 ? 1.0 - inside : inside;
        }

        float3 voxelNormal = float3(0.0, 0.0, 0.0);
        voxelNormal[axis] = side;
        // Same normal transform as voxel.slang shadeHit
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

// Opaque fills the list from the bottom with args 0 to 3 and glass from the top with args 4 to 7
void appendVoxels(uint index, uint mask, bool glass) {
    uint count = countbits(mask);
    if (count == 0u) {
        return;
    }
    uint base = 0u;
    InterlockedAdd(gDrawArgs[glass ? 5u : 1u], count, base);
    uint listCapacity = gParams.capacity * 8u;
    uint written = 0u;
    for (uint bit = 0u; bit < 8u; ++bit) {
        if (((mask >> bit) & 1u) != 0u && base + written < listCapacity) {
            uint slot = glass ? listCapacity - 1u - (base + written) : base + written;
            gDrawList[slot] = (index << 3) | bit;
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
    gDrawArgs[4] = 36u;
    gDrawArgs[5] = 0u;
    gDrawArgs[6] = 0u;
    gDrawArgs[7] = 0u;
}

// Over the particle cap the oldest get only the fade left
[shader("compute")]
[numthreads(kGroupSize, 1, 1)]
void retireMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gParams.retireCount) {
        return;
    }
    uint slot = gRetires[id.x];
    if (slot >= gParams.capacity) {
        return;
    }
    DebrisMotion motion = gMotion[slot];
    motion.lifetime = min(motion.lifetime, motion.age + gParams.fadeOut);
    gMotion[slot] = motion;
}

[shader("compute")]
[numthreads(kGroupSize, 1, 1)]
void integrateMain(uint3 id : SV_DispatchThreadID, uint3 groupThread : SV_GroupThreadID) {
    // Before any early return so every thread reaches the barrier
    uint volumeCount = min(gParams.volumeCount, kMaxVolumes);
    for (uint v = groupThread.x; v < volumeCount; v += kGroupSize) {
        sBoxMin[v] = gVolumes[v].boxMin;
        sBoxMax[v] = gVolumes[v].boxMax;
    }
    GroupMemoryBarrierWithGroupSync();

    uint i = id.x;
    if (i >= gParams.capacity) {
        return;
    }

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
    if (motion.age > motion.lifetime) {
        particle.packed = 0u;
        gCurrent[i] = particle;
        gMotion[i] = motion;
        return;
    }
    particle.scale = displayScale(motion);
    const bool glass = (particle.packed & kFlagGlass) != 0u;

    // Farthest point plus the floor probe below it plus how far gravity and velocity carry it this step
    float travel = (length(particle.velocity) + length(gParams.gravity) * stepTime) * stepTime;
    float reach = 1.74 * gParams.voxelSize * motion.baseScale + 1.5 * gParams.voxelSize + travel;
    Candidates candidates = gatherCandidates(particle.position, reach);

    if ((particle.packed & kFlagAsleep) != 0u) {
        // Wakes once nothing supports it
        // probes under the corners
        float sleepUnit = gParams.voxelSize * motion.baseScale;
        bool supported = false;
        for (int pz = -1; pz <= 1 && !supported; ++pz) {
            for (int py = -1; py <= 1 && !supported; ++py) {
                for (int px = -1; px <= 1 && !supported; ++px) {
                    int3 p = int3(px, py, pz);
                    if (all(p == 0) || !latticePresent(mask, p)) {
                        continue;
                    }
                    float3 corner = particle.position + rotateByQuat(particle.rotation, float3(p) * sleepUnit);
                    supported = corner.z <= kFloorZ + 2.0 * kContactSlop ||
                                contactAt(corner - float3(0.0, 0.0, 0.25 * gParams.voxelSize), candidates).hit;
                }
            }
        }

        if (supported) {
            gCurrent[i] = particle;
            gMotion[i] = motion;
            appendVoxels(i, mask, glass);
            return;
        }
        particle.packed &= ~kFlagAsleep;
        motion.restSteps = 0u;
    }

    bool touching = false;
    for (uint s = 0u; s < gParams.substeps; ++s) {
        float dt = gParams.dt;
        particle.velocity += gParams.gravity * dt;

        uint sweeps = clamp(uint(ceil(length(particle.velocity) * dt / (0.5 * gParams.voxelSize))), 1u, kMaxSweeps);
        float sweepDt = dt / float(sweeps);
        for (uint w = 0u; w < sweeps; ++w) {
            particle.position += particle.velocity * sweepDt;

            float spin = length(motion.angular);
            if (spin > 1e-5) {
                particle.rotation = normalize(particle.rotation + 0.5 * sweepDt * quatMul(float4(motion.angular, 0.0), particle.rotation));
            }

            // The piece is tested by the corners of its cubes
            float unit = gParams.voxelSize * motion.baseScale;
            float floorDepth = 0.0;
            Contact deepest;
            deepest.hit = false;
            deepest.normal = float3(0.0, 0.0, 1.0);
            deepest.depth = 0.0;
            float3 hitOffsetSum = float3(0.0, 0.0, 0.0);
            float hitCount = 0.0;
            for (int pz = -1; pz <= 1; ++pz) {
                for (int py = -1; py <= 1; ++py) {
                    for (int px = -1; px <= 1; ++px) {
                        int3 p = int3(px, py, pz);
                        if (all(p == 0) || !latticePresent(mask, p)) {
                            continue;
                        }
                        float3 offset = rotateByQuat(particle.rotation, float3(p) * unit);
                        float3 point = particle.position + offset;
                        floorDepth = max(floorDepth, kFloorZ - point.z);
                        Contact contact = contactAt(point, candidates);
                        if (contact.hit) {
                            hitOffsetSum += offset;
                            hitCount += 1.0;
                            if (!deepest.hit || contact.depth > deepest.depth) {
                                deepest = contact;
                            }
                        }
                    }
                }
            }

            // Nothing goes under the ground plane
            if (floorDepth > 0.0) {
                particle.position.z += floorDepth;
                // It stops dead, otherwise it keeps spinning and sliding along the floor
                particle.velocity = float3(0.0, 0.0, 0.0);
                motion.angular = float3(0.0, 0.0, 0.0);
                touching = true;
            }

            if (deepest.hit) {
                touching = true;
                particle.position += deepest.normal * (deepest.depth + kContactSlop);

                float approach = dot(particle.velocity, deepest.normal);
                if (approach < 0.0) {
                    float3 before = particle.velocity;
                    float3 tangent = particle.velocity - approach * deepest.normal;
                    float bounce = -approach > kBounceSpeed ? -approach * gParams.restitution : 0.0;

                    // Friction only takes what the normal impulse allows
                    float tangentSpeed = length(tangent);
                    float grip = min(tangentSpeed, gParams.friction * (-approach + bounce));
                    if (tangentSpeed > 1e-6) {
                        tangent -= tangent * (grip / tangentSpeed);
                    }
                    particle.velocity = tangent + bounce * deepest.normal;

                    if (-approach > kBounceSpeed) {
                        // cube inertia
                        float3 contactOffset = hitOffsetSum / max(hitCount, 1.0);
                        float size = gParams.voxelSize * motion.baseScale;
                        motion.angular *= saturate(1.0 - gParams.friction);
                        motion.angular += cross(contactOffset, particle.velocity - before) * (1.5 / max(size * size, 1e-6));
                        float angularSpeed = length(motion.angular);
                        if (angularSpeed > kMaxSpin) {
                            motion.angular *= kMaxSpin / angularSpeed;
                        }
                    } else {
                        motion.angular *= 0.5;
                    }
                }
            }
        }
    }

    if (touching && length(particle.velocity) < kSleepSpeed && length(motion.angular) < kSleepSpin) {
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
    appendVoxels(i, mask, glass);
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
    particle.glass = spawn.glass;
    particle.pad0 = 0u;
    particle.pad1 = 0u;

    DebrisMotion motion;
    motion.angular = spawn.angular;
    motion.age = 0.0;
    motion.restSteps = 0u;
    motion.lifetime = spawn.lifetime;
    motion.baseScale = spawn.scale;
    motion.pad0 = 0u;
    gMotion[spawn.slot] = motion;

    particle.scale = displayScale(motion);
    gCurrent[spawn.slot] = particle;

    appendVoxels(spawn.slot, spawn.packed & kMaskBits, (spawn.packed & kFlagGlass) != 0u);
}
