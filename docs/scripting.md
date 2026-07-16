# Lua Scripting

> Authors: Xein
>
> Created on: 13 Jul 2026

Nodes can carry Lua scripts. A script is a `.lua` asset attached through the node's
`Scripts` array in the inspector; from that moment its lifecycle functions run exactly
like reflected C++ methods, its variables show up in the inspector, and it can read and
write everything the node reflects. Most game code is expected to live here.

```lua
---@class Rotator : Node3D
local node = {}

node.degrees_per_second = 45.0

function node:tick()
	local spin = quat.angleAxis(self.degrees_per_second * Time.delta(), vec3(0, 1, 0))
	self.rotation = spin * self.rotation
end

return node
```

## Motivation

The engine already had a strong reflection system: fields, methods, groups and tick
functions are all described by generated `NodeInfo` data. Scripting plugs into that
instead of inventing a parallel world. A script's `self` is a table whose metatable
routes unknown keys through the node's reflection, so `self.rotation` is the same
rotation C++ sees, `self:call("foo")` walks the same method chain, and a script-only
`tick()` registers in the dependency graph like any C++ tick would—waves, dependencies
and all.

Scripts run on a pool of independent Lua interpreters (one per worker thread plus one for
the main thread). Each node's scripts bind to one interpreter at load, so nodes on
different interpreters execute Lua in parallel during tick waves; nothing crosses between
interpreters except plain values marshalled through C++. This keeps the scheduler's
parallelism without a global interpreter lock.

Everything a script exports is typed at load time into a schema: the editor streams it,
draws it, and writes edits back live. The same schema powers hot reload, where an edited
source file swaps in while tuned values survive.

## Uses

### Anatomy of a script

A script is a chunk that returns a table. Functions on that table with lifecycle names
are called by the engine, everything else becomes an inspector variable:

`load`, `save`, `pre_init`, `init`, `begin`, `earlyTick`, `tick`, `postPhysics`,
`lateTick`, `_end` (frame end; `end` is a reserved keyword in lua), `onEnable`,
`onDisable`, `destroy`.

All of them receive the script table as `self`. Reflected fields are reachable with or
without the `m_` prefix (`self.health` finds a reflected `m_health`).

### Inspector variables

Non-function keys export to the inspector. A nested table renders as a group, a table
inside that as a subgroup, deeper nesting is not supported. Keys starting with `_` stay
private. Order in the inspector follows the order of assignment in the source.

Supported types: booleans, integers, numbers, strings, `vec2/vec3/vec4`, `color3/color4`
(distinct types so you get a picker instead of three number fields), node references,
asset references, and flat arrays of all of those. References declare their accepted type
with a marker: `target = Node`, `enemy = Enemy3D`, `icon = Texture`. An empty array
declares its element type the same way: `loot = { Asset }`.

### Talking to nodes

`self` doubles as the node: `find`, `search`, `create`, `call`, `addDependsOn`,
`enabled`, `name`, `uid` and `exists` are all available on it and on any node reference.
`find`/`search` accept the same name queries as C++ (bare name, slash path, `node://`
URIs with the `root`/`world`/`global` keywords); UIDs are not valid strings
`call` invokes a reflected C++ method base-to-derived and then any same-named function on
the target's scripts.

### Hot reload

In dev builds the engine watches attached script sources and reloads them in place when
the file changes. Exported variables keep their values as long as their name, location
and type survive the edit; anything else falls back to the new default. Tick schedules
recompute, so adding a `tick()` to a running script just works.

### Editor completion

Opening a project writes `.toast/lua/` (definition stubs for the whole API, plus
generated per-type stubs for every reflected engine and game class) and a `.luarc.json`
pointing at it. With [lua-language-server](https://luals.github.io/) installed, annotate
your script table with the node type it sits on and completion covers reflected fields
too:

```lua
---@class PlayerScript : Node3D
local Node = {}
```

Note that the table can be named whatever you would like, I am just using `Node` as an
example

### Logging and profiling

`print` and `warn` land in the `Lua` log sink; `toast.trace/info/warn/error` pick the
severity explicitly. Follow the logging rules: traces for debugging, never log per
frame. Script errors carry full Lua stack traces.

Every script call already shows up as a zone in Tracy (named after the script and
function). For finer detail scripts can open their own zones with `tracy.ZoneBeginN`
/ `tracy.ZoneEnd`, and each interpreter's heap is plotted once per second.

## Performance

Lua execution serializes per interpreter, not globally. Nodes bound to different
interpreters tick in parallel; a node calling into another node's scripts on a different
interpreter blocks briefly on that interpreter's lock. Two nodes on different
interpreters synchronously calling each other's scripts at the same time is detected and
dropped with an error instead of deadlocking — don't build that cycle.

Phase dispatch is free for scripts that don't implement the phase: presence is cached in
a bitmask at load, so a node whose script only defines `init` costs nothing per frame.

## Unit tests

`tests/scripting` covers schema extraction (types, groups, ordering, collisions),
scheduling of Lua-only tick functions, hot-reload value preservation, and error
reporting. The reflection generator's stub emission is covered by its own cargo tests.

## Expansion

Sending engine events from scripts (`toast.send`) is the next step once event reflection
matures; the conversion layer it needs already exists. A per-wave "Lua lane" could
recover the last bit of parallelism lost to interpreter locks if profiling ever shows
contention.
