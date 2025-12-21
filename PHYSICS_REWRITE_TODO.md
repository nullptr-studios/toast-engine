# Physics System Rewrite TODO

This document tracks the physics-related functionality that was removed and needs to be reimplemented when the new physics system is completed.

## Removed Components

### 1. Physics System Instance
**Location:** `inc/Engine/Toast/Engine.hpp` and `src/Toast/Engine.cpp`
- Removed `#include "../src/Physics/PhysicsSystem.hpp"`
- Removed `std::unique_ptr<physics::PhysicsSystem> m_physicsSystem;` member variable
- Removed initialization: `m_physicsSystem = std::make_unique<physics::PhysicsSystem>();`

**TODO:** 
- Re-add physics system include once the new implementation exists
- Re-add physics system member variable to Engine class
- Re-initialize physics system in Engine::Init()

---

### 2. Scene Unload Physics Synchronization
**Location:** `src/Toast/World.cpp` - `World::UnloadScene(Scene* scene)`
- Removed physics halt request when unloading scenes
- Previously called `physics::PhysicsSystem::RequestHaltSimulation()`

**TODO:**
- Implement scene unload synchronization with physics thread
- Ensure physics simulation is safely halted before scene destruction
- Consider thread-safe scene removal mechanism

**Original Code:**
```cpp
// Request physics halt
physics::PhysicsSystem::RequestHaltSimulation();
```

---

### 3. Scene Destruction Physics Confirmation
**Location:** `src/Toast/World.cpp` - `World::ProcessDestroyQueue()`
- Removed physics confirmation wait loop when destroying scenes
- Previously waited for physics thread acknowledgment with retry mechanism
- Had timeout protection (MAX_RETRIES = 100)

**TODO:**
- Implement proper synchronization between game thread and physics thread for scene destruction
- Add confirmation mechanism: `WaitForAnswer()` and `ReceivedAnswer()` methods
- Include timeout protection to prevent infinite waiting
- Handle edge case where physics doesn't respond in time

**Original Code:**
```cpp
if (obj->base_type() == SceneT) {
    // Wait for physics confirmation (with timeout to avoid infinite rescheduling)
    int retries = 0;
    const int MAX_RETRIES = 100;
    while (!physics::PhysicsSystem::WaitForAnswer() && retries < MAX_RETRIES) {
        retries++;
        // Busy wait for physics
    }
    
    if (retries >= MAX_RETRIES) {
        // Physics didn't respond - force erase anyway
        TOAST_WARN("Physics didn't respond after {0} retries, force erasing scene {1}", MAX_RETRIES, obj->id());
        m_children.erase(obj->id());
    } else {
        // Physics confirmed
        m_children.erase(obj->id());
        physics::PhysicsSystem::ReceivedAnswer();
    }
}
```

---

## Existing Physics Infrastructure (Not Removed)

These components are still in place and will need to interact with the new physics system:

### Time System - Physics Ticking
**Location:** `inc/Engine/Core/Time.hpp` and `src/Core/Time.cpp`
- `void PhysTick()` - Updates physics clocks
- `clock_t::time_point m_nowPhys` - Timestamp for physics frame
- `clock_t::time_point m_previousPhys` - Timestamp for last physics frame

**Status:** ✅ Still present
**TODO:** Hook up to new physics system when ready

---

### Object Physics Tick
**Location:** `inc/Engine/Toast/Objects/Object.hpp` and `src/Toast/Objects/Object.cpp`
- `virtual void PhysTick()` - Virtual function for physics logic (user-overridable)
- `void _PhysTick()` - Internal physics tick that calls user PhysTick and recurses to children

**Status:** ✅ Still present
**TODO:** Ensure new physics system calls `_PhysTick()` appropriately

---

### World Physics Tick
**Location:** `inc/Engine/Toast/World.hpp` and `src/Toast/World.cpp`
- `void PhysTick()` - Iterates through tickable scenes and calls their `_PhysTick()`
- Includes editor scene physics ticking under `TOAST_EDITOR` flag

**Status:** ✅ Still present
**TODO:** Physics system should call `World::PhysTick()` on physics thread

---

### Component/Actor/Scene Physics Stubs
**Locations:**
- `inc/Engine/Toast/Components/Component.hpp` - `void PhysTick() override { }`
- `inc/Engine/Toast/Objects/Actor.hpp` - `void PhysTick() override { }`
- `inc/Engine/Toast/Objects/Scene.hpp` - `void PhysTick() override { }`

**Status:** ✅ Still present (empty implementations)
**TODO:** Users can override these for custom physics behavior

---

## Implementation Notes

### Threading Considerations
The removed code suggests the physics system runs on a separate thread:
- Scene destruction required explicit synchronization
- Busy-wait loops were used (consider better alternatives: condition variables, futures)
- Timeout mechanisms were necessary

### Recommended Approach
1. **Thread-Safe Queue:** Use lock-free or mutex-protected queues for communication between game and physics threads
2. **Command Pattern:** Instead of direct calls, queue commands for the physics thread
3. **Double Buffering:** Consider double-buffering physics state for thread-safe reads
4. **Condition Variables:** Replace busy-wait loops with proper synchronization primitives

### Integration Points
When the new physics system is complete, integrate at these points:
1. `Engine::Init()` - Create and initialize physics system
2. `World::UnloadScene()` - Request physics halt before scene destruction  
3. `World::ProcessDestroyQueue()` - Wait for physics confirmation for scene objects
4. Physics thread main loop - Call `World::PhysTick()` at fixed timestep

---

## Spine Physics References (NOT Related to Engine Physics)

**Note:** The following Spine-related physics references are **NOT** part of the engine physics system and should **NOT** be modified:

- `src/Toast/Components/SpineRendererComponent.cpp` - Uses `spine::Physics_None` and `spine::Physics_Update`
- `src/Toast/Components/AtlasRendererComponent.cpp` - Uses `spine::Physics_None` and `spine::Physics_Update`

These are Spine animation library physics constraints and are unrelated to the engine's physics system.

---

## Status
- [x] Removed all physics system references
- [x] Engine compiles successfully without physics system
- [ ] Implement new physics system
- [ ] Restore physics integration following this guide
- [ ] Test scene loading/unloading with physics
- [ ] Test object destruction synchronization
- [ ] Performance test physics thread separation
