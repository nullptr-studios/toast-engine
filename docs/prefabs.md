# Prefabs

> Authors: Xein
>
> Created on: 18 Jun 2026

A prefab is a serialized node tree stored on disk. The first node in the file is always
the tree root. Prefabs come in two formats:

- **Text** (`.node`), human-readable, used in the editor
- **Binary** (`.tnode`), compact, used at runtime

The format version is stored in the binary header. Bump `_detail::format_version` in
`prefab.hpp` whenever the binary layout changes.

## Loading a prefab

The easiest way is to spawn it as a child of an existing node:

```cpp
// By UID (resolved through the asset manifest)
Box<Node> instance = my_node->spawn(uid);

// By virtual URI
Box<Node> instance = my_node->spawn("assets://characters/knight.node");
```

You can also load a prefab into the cache and promote it later:

```cpp
World::loadNode("assets://levels/lobby.node");
// ... later ...
World::setRoot(*World::findCached("lobby"));
```

## Prefab instances

When a prefab is instantiated, the root node has `isInstanceRoot()` returning `true`.
Nodes inside the instance have `m_prefab_interior = true`, which makes `find()` stop
traversal at the instance boundary. This keeps instances opaque; you can't accidentally
reach into another prefab's internals with a search.

The `sourcePrefab()` accessor on any node returns the `Handle<Prefab>` the node
came from (empty if the node was created at runtime).

## Overrides

Prefabs support field overrides. When a prefab embeds another prefab as a child, the
outer file can override field values on the inner nodes. The serializer writes only
changed values. On load, the inner prefab's defaults are applied first, then the outer
overrides are layered on top.

The set of valid UIDs for inner prefab nodes is stored in `m_allowed_uids` so that the
loader can detect collisions between embedded prefab UID spaces.

## Self-referencing prefabs

A prefab that refers to itself by UID would cause infinite recursion. The `m_self_uid`
field breaks this: during instantiation the UID is checked against the asset chain in
`InstantiateContext::asset_chain`, and the recursive spawn is skipped.

## Saving

Implement `ISaveable` on your asset class and call `serialize(SaveMode::editor)` (text)
or `serialize(SaveMode::game)` (binary). `Prefab` handles both:

```cpp
Prefab pf(root_node, root_uid);
auto text   = pf.toFile();    // text format
auto binary = pf.toBinary();  // binary format
```
