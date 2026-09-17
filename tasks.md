  ## Week 1 — Rigid-body physics

  Days 1–6 are complete on `feature/physics`: fixed 60 Hz accumulator, generational body/shape handles, recorded manifolds,
  full contact solver, sphere/box/capsule pairs, persistent manifold cache with warm starting, AABB-tree broad phase,
  sleeping, and multithreaded AABB/narrow-phase/island solving.

  Day 7 (editor integration, runtime shape replacement, compound interface, stress scene) moves to Day 13. The GPU
  capability items are dropped for now.

  ## Week 2 — Voxel destruction and fracture

  Dario's `voxel` module already provides the storage (`Volume`, `BrickPool`, `BrickOccupancy`), connectivity
  (`analyseConnectivity`), mass properties (`MassMoments`, `resolve`), surface classification (`VolumeSurface`), palettes,
  and physical materials. Week 2 is about plugging that into the physics pipeline: a voxel volume becomes a `ShapeType`
  like sphere, box, and capsule, and destruction becomes a shape revision plus body creation through the same paths every
  other shape uses.

  ### Day 8 — Voxel shape type and primitive-versus-voxel contacts

  - [ ] Add `ShapeType::voxel` to the physics shape union.
    - [ ] Store a non-owning pointer to the `voxel::Volume`, the `voxel::Palette`, and the `voxel::MaterialLibrary`.
    - [ ] Store the local voxel-to-body transform (voxel origin offset plus lattice orientation).
    - [ ] Store a cached local AABB from `brickDims()` so the broad phase never touches bricks.
  - [ ] Add `voxel_face` and `voxel_edge` to `FeatureType`.
    - [ ] Pack brick slot, local voxel index, and face axis/sign into the 56-bit contact-feature payload.
    - [ ] Keep the packing deterministic so the manifold cache matches contacts across steps.
  - [ ] Add `Simulator::createVoxelShape` next to `createSphere`, `createBox`, `createCapsule`.
  - [ ] Add `NarrowPhasePairType` entries for sphere–voxel, box–voxel, capsule–voxel, and voxel–voxel.
    - [ ] Voxel–voxel returns no manifold until Day 11.
  - [ ] Let `VoxelNode` register with the simulator the same way `Rigidbody` does.
    - [ ] A `VoxelNode` under a `StaticRigidbody` or `DynamicRigidbody` contributes a voxel shape instead of a collider.
    - [ ] A `VoxelNode` with no rigidbody parent registers itself as a static body (world geometry).
    - [ ] Bind the shape to the node's `revision()` so a model swap becomes a shape revision bump.
  - [ ] Add voxel bounds to `BroadPhase::calculateBounds` from the rotated local AABB.
  - [ ] Write a shared voxel-query helper for the narrow phase.
    - [ ] Transform the primitive into voxel-local space using the body transform and the shape's lattice placement.
    - [ ] Compute the primitive's local AABB and iterate only the bricks it overlaps.
    - [ ] Skip `BrickTag::empty` bricks and use `k_full_brick` for `BrickTag::uniform` bricks.
    - [ ] Reject voxels whose material is `k_material_passable` through `resolveMaterialIndex`.
  - [ ] Use `VolumeSurface` classification to test only exposed voxels.
    - [ ] Keep a `VolumeSurface` per voxel shape and rebuild it when the shape revision changes.
    - [ ] Use the packed normal index from `classifyFromNeighbours` as the contact normal for face and edge voxels.
  - [ ] Implement sphere–voxel: closest point on the voxel cube to the sphere centre, penetration from radius.
  - [ ] Implement capsule–voxel by sampling the capsule segment against overlapping voxels.
  - [ ] Implement box–voxel by treating each candidate voxel as a unit box and reusing the box–box SAT path.
  - [ ] Handle several normals per pair.
    - [ ] A `Manifold` carries one normal, but a voxel shape can touch a primitive on a floor and a wall at once.
    - [ ] Emit one manifold per distinct outward normal for the same pair and include the normal index in the sort key.
    - [ ] Make `updateCache` and `buildIslands` accept more than one manifold per `BroadPhasePair`.
  - [ ] Reduce contacts to the best four per manifold: deepest first, then spread by distance.
  - [ ] Pull friction and restitution from the `PhysicalMaterial` of the hit voxel instead of the shape material.
  - [ ] Add the voxel pair types to `NarrowPhase::collide` with the same flip convention as the existing pairs.
  - [ ] Add `tests/physics/` with a test registry entry.
    - [ ] A voxel shape registers, gets bounds, and shows up in broad-phase pairs against a sphere.
    - [ ] Sphere resting on a flat uniform-brick floor produces one manifold with normal +Z.
    - [ ] Sphere in a one-voxel-wide trench gets side contacts with the right feature IDs.
    - [ ] Box on a stair-step surface gets at most four contacts per normal.
    - [ ] Same result with swapped pair ordering.
  - [ ] Debug draw: contact points and normals for voxel contacts using the existing contact visualiser.

  End-of-day result: a `VoxelNode` is a physics shape, and spheres, boxes, and capsules collide with it through the
  recorded-manifold pipeline with per-voxel materials.

  ### Day 9 — Destruction commands and shape revisions

  - [ ] Define a `DamageCommand` queue on the simulator.
    - [ ] Shape ID, world-space centre, radius, energy, and source body.
    - [ ] Commands are recorded from gameplay, contacts, or the editor and applied at the start of the next physics step.
    - [ ] Never mutate a `Volume` from a worker thread; apply commands on the main thread before broad phase.
  - [ ] Implement sphere-shaped voxel removal.
    - [ ] Convert the world sphere to voxel space with the shape's lattice placement.
    - [ ] Walk only the bricks the sphere overlaps and call `Volume::setVoxel` for solid voxels inside the radius.
    - [ ] Skip `k_material_indestructible` voxels and voxels whose `toughness` exceeds the command energy.
    - [ ] Collect the `VoxelWrite` results to know which bricks changed and which became empty.
  - [ ] Track dirty bricks per shape for the step.
    - [ ] Repair the `VolumeSurface` with `repairBrickRegion` for each dirty brick.
    - [ ] Bump the shape revision so cached manifolds against the shape are dropped.
    - [ ] Bump the `VoxelNode` revision so Dario's renderer re-uploads the volume.
  - [ ] Wake every dynamic body whose broad-phase AABB overlaps the damaged region.
  - [ ] Convert contact impulses into damage.
    - [ ] After solving, read the accumulated normal impulse per constraint touching a voxel shape.
    - [ ] Compare impulse against the voxel material's `toughness` and enqueue a `DamageCommand` when it exceeds it.
    - [ ] Use `shatter_radius` from the material as the removal radius.
  - [ ] Expose a scripting/gameplay entry point `Simulator::applyExplosion(position, radius, energy)`.
    - [ ] Query the broad-phase tree for voxel shapes overlapping the sphere and enqueue one command per shape.
  - [ ] Tests.
    - [ ] Removal inside one brick, across a brick boundary, and across the volume edge.
    - [ ] Indestructible voxels survive.
    - [ ] A resting sphere over removed voxels wakes and falls on the next step.
  - [ ] Debug draw: highlight dirty bricks for one frame after a damage command.

  End-of-day result: an explosion removes voxels, the renderer and the collision surface both update, and bodies resting on
  the removed voxels react.

  ### Day 10 — Connectivity, anchoring, and detached components

  - [ ] Decide anchoring rules for a voxel shape.
    - [ ] Static voxel bodies anchor every component touching the volume's bottom face (`z == 0`) by default.
    - [ ] Allow an explicit anchor mask per shape so walls anchored to the side or ceiling can be authored.
    - [ ] Dynamic voxel bodies have no anchors; every component is free.
  - [ ] Run `analyseConnectivity` after a damage command.
    - [ ] Only when a brick became empty or a removed voxel was on a brick face, since interior removals cannot split.
    - [ ] Run it as a worker job on an immutable snapshot of the shape's `BrickEntry` grid plus a revision number.
    - [ ] Reject the result if the shape revision changed while the job was in flight.
  - [ ] Classify components.
    - [ ] Anchored: any `BrickPiece` in the component touches the anchor mask.
    - [ ] Detached: no anchored piece; this component must become a fragment.
    - [ ] Below `k_min_fragment_voxels`: dropped instead of extracted.
  - [ ] Record a `DetachedComponent` list per shape without mutating the volume.
    - [ ] Store the component index, its `BrickPiece` list, and the voxel count.
    - [ ] Sort deterministically by first brick slot so results match across single- and multithreaded runs.
  - [ ] Add a `Simulator::debugComponents()` query that returns component labels per brick for the editor.
  - [ ] Tests.
    - [ ] A pillar cut in half yields one anchored and one detached component.
    - [ ] A bridge with both ends anchored stays one anchored component until the second cut.
    - [ ] A component spanning two bricks through a face-adjacent voxel stays one component.
    - [ ] A stale revision rejects the job result.
  - [ ] Debug draw: colour bricks by component index, red for detached.

  End-of-day result: cutting the supports of a structure produces a deterministic list of components that must fall.

  ### Day 11 — Fragment extraction and voxel–voxel collision

  - [ ] Extract each detached component into a new `Volume`.
    - [ ] Allocate from `runtimeBrickPool()` and copy only the component's voxels, preserving palette indices.
    - [ ] Size the new volume to the component's brick bounds so fragments stay small.
    - [ ] Clear the extracted voxels from the source volume with `setVoxel` and let empty bricks free themselves.
    - [ ] Collapse touched bricks with `tryCollapseUniform`.
  - [ ] Compute mass properties with `MassMoments` and `resolve`.
    - [ ] Accumulate density per voxel through `resolveMaterialIndex` and the material library.
    - [ ] Use the resulting centre of mass and inertia tensor directly in `Body` instead of the primitive formulas.
    - [ ] Extend `rebuildMassProperties` with a voxel case that uses the resolved inertia.
  - [ ] Create the fragment body through deferred commands.
    - [ ] Body position is the source body position plus the rotated component offset, so the fragment does not move on
          the frame it is born.
    - [ ] Inherit linear velocity at the component centre from the source body.
    - [ ] Create the voxel shape and a `VoxelNode` with `VoxelMobility::dynamic` bound to it.
    - [ ] Assign the same palette so the fragment renders identically.
  - [ ] Implement voxel–voxel contact generation.
    - [ ] Transform the smaller volume's surface voxels into the larger volume's space.
    - [ ] Test each surface voxel as a unit box against the larger volume with the Day 8 helper.
    - [ ] Cap candidate voxels per pair and fall back to the four deepest contacts.
  - [ ] Let fragments collide with the original structure and with other fragments.
  - [ ] Commit extraction atomically inside the physics step so the renderer never sees a half-extracted structure.
  - [ ] Tests.
    - [ ] A detached block becomes a dynamic body whose mass equals density times voxel count.
    - [ ] The fragment's centre of mass matches the voxel centroid.
    - [ ] The source volume no longer contains the extracted voxels.
    - [ ] Two fragments resting on each other produce voxel–voxel contacts.

  End-of-day result: disconnected components become falling voxel rigid bodies that collide with everything else.

  ### Day 12 — Recursive fracture and budgets

  - [ ] Allow damage commands to target dynamic voxel shapes.
    - [ ] Reuse the Day 9 removal, dirty tracking, and revision bump on moving fragments.
  - [ ] Run connectivity on fragments after damage and split them again.
    - [ ] Preserve linear momentum by giving each child the parent's velocity at the child centre of mass.
    - [ ] Preserve angular velocity as an approximation.
    - [ ] Reuse the Day 11 extraction path so the code is one function for static and dynamic sources.
  - [ ] Fragment lifecycle.
    - [ ] Remove fragments below `k_min_fragment_voxels` or that stay asleep past a configurable lifetime.
    - [ ] Free the fragment's bricks back to the pool through `Volume`'s destructor.
    - [ ] Cap fragments created per step and carry the remainder to the next step.
    - [ ] Cap active fragments; when exceeded, sleep the oldest ones first.
  - [ ] Multithreading.
    - [ ] Run `VolumeSurface` repair and connectivity jobs on the thread pool with revision-checked outputs.
    - [ ] Keep all `Volume` writes and pool allocations on the main thread.
    - [ ] Verify with the existing single-threaded/multithreaded compare mode.
  - [ ] Profiling.
    - [ ] Tracy zones for damage apply, surface repair, connectivity, extraction, and voxel narrow-phase pairs.
    - [ ] Counters for dirty bricks, components found, fragments created, and fragments freed.
  - [ ] Tests.
    - [ ] Hitting a falling fragment splits it into two bodies with total mass preserved.
    - [ ] Repeated destruction over 500 steps leaves the brick pool without leaks.
    - [ ] Fragment cap holds under a large explosion.
  - [ ] Run the destruction stress scene and note the frame budget.

  End-of-day result: fragments collide, break again, and the whole pipeline stays inside budget with no pool leaks.

  ### Day 13 — Editor integration and API stabilisation

  - [ ] Finish the moved Day 7 items.
    - [ ] Create and destroy bodies only through the deferred command queue so scripts can spawn mid-tick.
    - [ ] Add render interpolation between the previous and current physics transforms using the accumulator alpha.
    - [ ] Support runtime shape replacement through a revision bump instead of destroy-and-create.
    - [ ] Support multiple shapes per body in `rebuildMassProperties` by summing inertia tensors.
    - [ ] Run the mixed-body stress scene and fix critical rigid-body bugs.
    - [ ] Freeze the physics public API for the demo.
  - [ ] Editor UX.
    - [ ] Inspector fields on `VoxelNode` for anchor mask, destructible toggle, and material library override.
    - [ ] Inspector fields on `DynamicRigidbody` showing centre of mass and inertia from the voxel volume.
    - [ ] An explosion tool: click in the viewport to enqueue `applyExplosion` at the hit point.
    - [ ] Debug toggles for contacts, dirty bricks, components, fragment AABBs, and the broad-phase tree.
    - [ ] Inspector warnings when a voxel node has no palette or its material library fails `validateLibrary`.
  - [ ] Demo scene.
    - [ ] Build the presentation scene: a voxel building on static terrain with a few primitive props.
    - [ ] Script the explosion sequence and a free-fly camera path.
  - [ ] Tune gravity, friction, restitution, toughness, shatter radius, and fragment limits per material.

  End-of-day result: the editor can author destructible voxel scenes and trigger destruction without code changes.

  ### Day 14 — Demo stabilisation only

  - [ ] Stop adding features.
  - [ ] Fix crashes, invalid handles, stale jobs, and synchronisation bugs.
  - [ ] Improve explosion feedback.
    - [ ] Dust from the `dust_colour` of the removed material.
    - [ ] Impact sound from `impact_sound`.
    - [ ] Camera shake scaled by removed voxel count.
  - [ ] Hide or remove distracting debug output while keeping the Day 13 toggles.
  - [ ] Test the demo from a clean launch repeatedly.
  - [ ] Test low and high framerates.
  - [ ] Test repeated destruction without restarting.
  - [ ] Capture a backup video of the working demo.
  - [ ] Tag the final stable build.

  End-of-day result: a repeatable presentation build, not a development sandbox.

  ## Non-negotiable checkpoints

  By the end of day 9 you need:

  - `ShapeType::voxel` in the narrow phase
  - Primitive-versus-voxel contacts with per-voxel materials
  - Damage commands that update collision and rendering

  By the end of day 11 you need:

  - Connectivity with anchoring
  - Detached rigid fragments with correct mass and inertia
  - Voxel–voxel contacts

  Days 12–14 add recursive fracture, budgets, editor UX, and polish. If you slip, simplify box–voxel and voxel–voxel
  contact quality before sacrificing fragment extraction or recursive destruction.

  ## Beyond the demo

  This is a year-long project. The tech demo freezes an API that must survive these extensions:

  - Voxel–voxel contacts move from surface-voxel sampling to brick-level occupancy intersection using `BrickOccupancy`
    bit operations.
  - Connectivity moves from whole-volume `analyseConnectivity` to incremental union-find seeded from dirty bricks.
  - Fragments merge back into the runtime pool with `Volume::instanceOf` for shared bricks when many identical fragments
    spawn.
  - Fire and material transformation use `k_material_flammable`, `transforms_to`, and `burn_rate`, which are already in
    `PhysicalMaterial`.
  - GPU compute for surface repair and connectivity once capability detection lands.
  - Joints and constraints for hinged or roped voxel pieces.
