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
    float maxSpeed;
    float maxSpin;
    uint pad0;
    uint pad1;
    uint pad2;
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
[[vk::binding(8, 0)]]
RWStructuredBuffer<uint> gGrid;

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
static const float kBounceSpeed = 0.5;
static const float kContactSlop = 0.001;    // next step does not start touching
static const float kFloorZ = 0.0f;          // lowest height a piece reaches
static const uint kMaxSweeps = 8;
static const float kSleepSpin = 1.0;
static const uint kMaxContacts = 12;
static const uint kContactPasses = 2;
static const float kMaxCorrection = 0.5;    // voxels a piece is moved out per sweep
static const float kInertia = 0.667;
static const float kAirDrag = 0.15;
static const float kSpinDrag = 0.3;
static const float kRollDrag = 6.0;
static const float kSpawnGrace = 0.25;      // seconds a new piece ignores being buried
static const uint kGridCells = 1u << 16;    // mirrors DebrisSimulation::k_grid_cells
static const uint kGridStride = 8;          // mirrors DebrisSimulation::k_grid_stride
static const uint kGridSlots = kGridStride - 1u;
static const float kCrowdRelax = 0.6;
static const float kMaxCrowdPush = 0.5;     // units a piece is moved by the others per step
static const float kCrowdFriction = 0.3;
static const float kSupportSpeed = 0.5;
static const float kWakeSpeed = 1.0;       // a hit faster than this wakes a sleeping piece

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

int3 gridCell(float3 position) {
    return int3(floor(position / (2.0 * gParams.voxelSize)));
}

uint gridHash(int3 cell) {
    return ((uint(cell.x) * 73856093u) ^ (uint(cell.y) * 19349663u) ^ (uint(cell.z) * 83492791u)) & (kGridCells - 1u);
}

float pieceRadius(uint mask, float scale) {
    return gParams.voxelSize * scale * lerp(0.6, 1.0, float(countbits(mask & kMaskBits)) / 8.0);
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

Contact contactAt(float3 world, Candidates candidates, bool buried) {
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
            if (!buried) {
                continue;
            }
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
void gridClearMain(uint3 id : SV_DispatchThreadID) {
    if (id.x < kGridCells) {
        gGrid[id.x * kGridStride] = 0u;
    }
}

// Bins every live piece by where the last step left it
[shader("compute")]
[numthreads(kGroupSize, 1, 1)]
void gridBuildMain(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= gParams.capacity || gParams.clear != 0u) {
        return;
    }
    DebrisParticle particle = gPrevious[i];
    if ((particle.packed & kMaskBits) == 0u) {
        return;
    }
    uint bucket = gridHash(gridCell(particle.position)) * kGridStride;
    uint place = 0u;
    InterlockedAdd(gGrid[bucket], 1u, place);
    if (place < kGridSlots) {
        gGrid[bucket + 1u + place] = i;
    }
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
    const bool glass = (particle.packed & kFlagGlass) != 0u;
    const bool asleep = (particle.packed & kFlagAsleep) != 0u;
    const float unit = gParams.voxelSize * motion.baseScale;

    // Other pieces count as spheres and are read where the last step left them
    float3 crowdPush = float3(0.0, 0.0, 0.0);
    float3 crowdVelocity = float3(0.0, 0.0, 0.0);
    float3 supportSum = float3(0.0, 0.0, 0.0);
    bool struck = false;
    {
        float radius = pieceRadius(mask, particle.scale);
        int3 base = gridCell(particle.position);
        for (int cz = -1; cz <= 1; ++cz) {
            for (int cy = -1; cy <= 1; ++cy) {
                for (int cx = -1; cx <= 1; ++cx) {
                    int3 cell = base + int3(cx, cy, cz);
                    uint bucket = gridHash(cell) * kGridStride;
                    uint count = min(gGrid[bucket], kGridSlots);
                    for (uint slot = 0u; slot < count; ++slot) {
                        uint j = gGrid[bucket + 1u + slot];
                        if (j == i || j >= gParams.capacity) {
                            continue;
                        }
                        DebrisParticle other = gPrevious[j];
                        uint otherMask = other.packed & kMaskBits;
                        if (otherMask == 0u || any(gridCell(other.position) != cell)) {
                            continue;
                        }

                        float3 delta = particle.position - other.position;
                        float reach = radius + pieceRadius(otherMask, other.scale);
                        float distanceSq = dot(delta, delta);
                        if (distanceSq >= reach * reach) {
                            continue;
                        }
                        float distance = sqrt(distanceSq);
                        float3 normal = distance > 1e-5 ? delta / distance : float3(0.0, 0.0, i > j ? 1.0 : -1.0);
                        bool otherAsleep = (other.packed & kFlagAsleep) != 0u;
                        float share = otherAsleep ? 1.0 : 0.5;

                        float extra = (reach - distance) * share - dot(crowdPush, normal);
                        if (extra > 0.0) {
                            crowdPush += normal * extra;
                        }

                        float3 relative = particle.velocity + crowdVelocity - other.velocity;
                        float closing = dot(relative, normal);
                        if (closing < 0.0) {
                            crowdVelocity -= normal * (closing * share);
                            float3 slide = relative - closing * normal;
                            crowdVelocity -= slide * (share * kCrowdFriction * saturate(gParams.friction));
                            struck = struck || (!otherAsleep && closing < -kWakeSpeed);
                        }
                        if (normal.z > 0.3 && dot(other.velocity, other.velocity) < kSupportSpeed * kSupportSpeed) {
                            supportSum += normal;
                        }
                    }
                }
            }
        }
    }
    const bool heldUp = dot(supportSum, supportSum) > 1e-6;
    const float3 supportNormal = heldUp ? normalize(supportSum) : float3(0.0, 0.0, 1.0);

    // Farthest point plus the floor probe below it plus how far gravity and velocity carry it this step
    float travel = (length(particle.velocity) + length(crowdVelocity) + length(gParams.gravity) * stepTime) * stepTime;
    float reach = 1.74 * unit + 1.5 * gParams.voxelSize + travel + kMaxCrowdPush * unit;
    Candidates candidates = gatherCandidates(particle.position, reach);
    const bool settled = motion.age > kSpawnGrace;

    if (asleep) {
        // Wakes once nothing supports it
        // probes under the corners
        bool supported = heldUp;
        for (int pz = -1; pz <= 1 && !supported; ++pz) {
            for (int py = -1; py <= 1 && !supported; ++py) {
                for (int px = -1; px <= 1 && !supported; ++px) {
                    int3 p = int3(px, py, pz);
                    if (all(p == 0) || !latticePresent(mask, p)) {
                        continue;
                    }
                    float3 corner = particle.position + rotateByQuat(particle.rotation, float3(p) * unit);
                    supported = corner.z <= kFloorZ + 2.0 * kContactSlop ||
                                contactAt(corner - float3(0.0, 0.0, 0.25 * gParams.voxelSize), candidates, true).hit;
                }
            }
        }

        if (supported && !struck) {
            particle.scale = displayScale(motion);
            gCurrent[i] = particle;
            gMotion[i] = motion;
            appendVoxels(i, mask, glass);
            return;
        }
        particle.packed &= ~kFlagAsleep;
        motion.restSteps = 0u;
    }

    particle.velocity += crowdVelocity;
    float crowdDistance = length(crowdPush) * kCrowdRelax;
    if (crowdDistance > 1e-6) {
        particle.position += crowdPush * (kCrowdRelax * min(1.0, kMaxCrowdPush * unit / crowdDistance));
    }

    // Unit mass solid cube two units wide
    const float invInertia = 1.0 / max(kInertia * unit * unit, 1e-8);
    bool touching = heldUp;
    for (uint s = 0u; s < gParams.substeps; ++s) {
        float dt = gParams.dt;
        particle.velocity += gParams.gravity * dt;
        particle.velocity *= 1.0 - saturate(kAirDrag * dt);
        motion.angular *= 1.0 - saturate(kSpinDrag * dt);
        if (heldUp) {
            // The pieces under it carry it
            float sinking = dot(particle.velocity, supportNormal);
            if (sinking < 0.0) {
                particle.velocity -= supportNormal * sinking;
            }
        }

        uint sweeps = clamp(uint(ceil(length(particle.velocity) * dt / (0.5 * gParams.voxelSize))), 1u, kMaxSweeps);
        float sweepDt = dt / float(sweeps);
        for (uint w = 0u; w < sweeps; ++w) {
            particle.position += particle.velocity * sweepDt;

            float spin = length(motion.angular);
            if (spin > 1e-5) {
                particle.rotation = normalize(particle.rotation + 0.5 * sweepDt * quatMul(float4(motion.angular, 0.0), particle.rotation));
            }

            // The piece is tested by the corners of its cubes
            float3 contactOffset[kMaxContacts];
            float3 contactNormal[kMaxContacts];
            uint contactCount = 0u;
            float3 correction = float3(0.0, 0.0, 0.0);
            for (int pz = -1; pz <= 1; ++pz) {
                for (int py = -1; py <= 1; ++py) {
                    for (int px = -1; px <= 1; ++px) {
                        int3 p = int3(px, py, pz);
                        if (all(p == 0) || !latticePresent(mask, p)) {
                            continue;
                        }
                        float3 offset = rotateByQuat(particle.rotation, float3(p) * unit);
                        float3 point = particle.position + offset;

                        // Nothing goes under the ground plane
                        Contact contact = contactAt(point, candidates, settled);
                        float floorDepth = kFloorZ - point.z;
                        if (floorDepth > 0.0 && (!contact.hit || floorDepth > contact.depth)) {
                            contact.hit = true;
                            contact.normal = float3(0.0, 0.0, 1.0);
                            contact.depth = floorDepth;
                        }
                        if (!contact.hit) {
                            continue;
                        }

                        // Leaves along every normal by the deepest point on it
                        float extra = contact.depth - dot(correction, contact.normal);
                        if (extra > 0.0) {
                            correction += contact.normal * extra;
                        }
                        if (contactCount < kMaxContacts) {
                            contactOffset[contactCount] = offset;
                            contactNormal[contactCount] = contact.normal;
                            ++contactCount;
                        }
                    }
                }
            }

            if (contactCount == 0u) {
                continue;
            }
            touching = true;

            float correctionLength = length(correction);
            if (correctionLength > 1e-7) {
                float moved = min(correctionLength, kMaxCorrection * gParams.voxelSize) + kContactSlop;
                particle.position += correction * (moved / correctionLength);
            }

            for (uint pass = 0u; pass < kContactPasses; ++pass) {
                for (uint c = 0u; c < contactCount; ++c) {
                    float3 arm = contactOffset[c];
                    float3 normal = contactNormal[c];
                    float3 pointVelocity = particle.velocity + cross(motion.angular, arm);
                    float approach = dot(pointVelocity, normal);
                    if (approach >= 0.0) {
                        continue;
                    }

                    float3 armNormal = cross(arm, normal);
                    float normalMass = 1.0 + invInertia * dot(armNormal, armNormal);
                    // slower impact sticks
                    float bounce = (pass == 0u && -approach > kBounceSpeed) ? gParams.restitution : 0.0;
                    float normalImpulse = -(1.0 + bounce) * approach / normalMass;
                    float3 impulse = normal * normalImpulse;

                    // Friction only takes what the normal impulse allows
                    float3 slide = pointVelocity - approach * normal;
                    float slideSpeed = length(slide);
                    if (slideSpeed > 1e-5) {
                        float3 tangent = slide / slideSpeed;
                        float3 armTangent = cross(arm, tangent);
                        float tangentMass = 1.0 + invInertia * dot(armTangent, armTangent);
                        impulse -= tangent * min(slideSpeed / tangentMass, gParams.friction * normalImpulse);
                    }

                    particle.velocity += impulse;
                    motion.angular += invInertia * cross(arm, impulse);
                }
            }

            motion.angular *= 1.0 - saturate(kRollDrag * sweepDt);
        }

        float speed = length(particle.velocity);
        if (gParams.maxSpeed > 0.0 && speed > gParams.maxSpeed) {
            particle.velocity *= gParams.maxSpeed / speed;
        }
        float angularSpeed = length(motion.angular);
        if (gParams.maxSpin > 0.0 && angularSpeed > gParams.maxSpin) {
            motion.angular *= gParams.maxSpin / angularSpeed;
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

    particle.scale = displayScale(motion);
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
