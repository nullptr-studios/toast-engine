# World

> Authors: Xein
>
> Created on: 18 Jun 2026

The World is the singleton that owns, ticks, and manages every Node that lives in the
engine. It is the only object that holds nodes in memory and the only one allowed to
create or destroy them.

## Nodes and the scene graph

Every entity in the engine is a `Node`. Nodes form a tree: each node has one parent and
any number of children. The root of the active tree is called the *world root*. Nodes
outside the tree but still resident in memory are either *cached* (sleeping) or *global*
(always awake, outside the main tree).

### Node states

| State     | Meaning |
|-----------|---------|
| `null`    | default; node has been allocated but not placed anywhere |
| `loading` | asset is being loaded asynchronously |
| `cached`  | resident in memory, disabled, not in any active tree |
| `root`    | part of the active world root tree |
| `global`  | active but outside the main tree (HUD, singletons, etc.) |
| `destroy` | queued for destruction; drained at the start of the next tick |

### Node types

| Type         | Meaning |
|--------------|---------|
| `null`       | not yet assigned |
| `child`      | regular node inside a tree |
| `root`       | root of a cached or global tree |
| `world_root` | the one root node currently active in the world |

## Loading and switching scenes

```cpp
// Load a prefab into memory (async); becomes a cached root node
World::loadNode(uid);
World::loadNode("assets://levels/lobby.node");

// Make it the active scene (returns the old root, now cached)
Box<Node> old_root = World::setRoot(node);

// Put an active node back into the cache
Box<Node> cached = World::cacheNode(node);

// Schedule a cached node for destruction (happens next tick)
World::destroyNode(node);
```

`setRoot` swaps the world root in one atomic step so there is no frame where the world
is empty (unless you cache the root yourself).

## Searching the tree

```cpp
// Single result; stops at prefab-instance boundaries
Box<Node> player = some_node->find("Player");

// Multiple results
std::vector<Box<Node>> enemies = some_node->search("Enemy");
```

`find` and `search` accept a `/`-separated path or a bare name, optionally behind a
`node://` prefix. Bare names match the first descendant with that name. Traversal stops
at nodes that are inside a prefab instance (they are opaque); `search` crosses those
boundaries instead. The first segment may be a scope keyword:

- `root/` — this node's nearest instance-root
- `world/` — the active world root
- `global/` — the world's global nodes

String queries match names only. To look up a node by UID use the typed overload:

```cpp
Box<Node> child = some_node->find(uid);
```

It resolves within the caller's local root (the nearest instance-root ancestor), so UIDs
only need to be unique inside one prefab instance. There is no UID form of `search`.

## Dependency graph

By default nodes tick in an unspecified order. If you need node A to always tick before
node B, register a dependency:

```cpp
World::instance()->registerDependency(a, b);
```

The World rebuilds the tick schedule whenever the dependency graph changes. Cycles are
legal; the nodes in a cycle are bundled into a `NodeCluster` and ticked sequentially in
declaration order within the cluster.

## Tick phases and wave execution

Every frame the world runs four tick phases in order:

1. **earlyTick**: input handling, early state updates
2. **tick**: game logic
3. **postPhysics**: responses to physics results
4. **lateTick**: cameras, UI, final transforms

Within each phase the scheduler groups nodes into *waves*. All nodes in one wave run in
parallel on the thread pool. The next wave starts only after the previous one finishes.
Wave indices are baked into `Node::m_wave` at schedule-build time so dispatch is O(1).

The wave assignment algorithm:

1. BFS flood-fill separates independent subgraphs
2. Tarjan SCC finds dependency cycles → `NodeCluster`
3. Wave index = `max(predecessor wave) + 1`; nodes with no predecessors land on wave 0
4. Per-phase pruning discards nodes that don't implement the relevant lifecycle function

## Workspace

`Workspace` is a lightweight version of World used by the editor viewport. It owns nodes
the same way but never ticks them and never builds a dependency graph. Use it whenever
you need to display or edit a node tree without running game logic.

```cpp
Workspace ws("toast::Node3D", uid);
```
