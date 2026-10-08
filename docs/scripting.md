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

`load`, `save`, `init`, `begin`, `earlyTick`, `tick`, `postPhysics`,
`lateTick`, `_end` (frame end; `end` is a reserved keyword in lua), `onEnable`,
`onDisable`, `destroy`, `editorTick`.

`editorTick` only runs while the node lives in an editor Workspace (a paused editor
tab), not in a PlayWorkspace (a playing editor tab) or a live game World.

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
`interactsWith`, `enabled`, `name`, `uid` and `exists` are all available on it and on any
node reference.
`find`/`search` accept the same name queries as C++ (bare name, slash path, `node://`
URIs with the `root`/`world`/`global` keywords); UIDs are not valid strings
`call` invokes a reflected C++ method base-to-derived and then any same-named function on
the target's scripts.
`interactsWith(other)` tells the scheduler that this node's scripts and `other`'s call each
other, see [Performance](#performance).

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

Lua execution serializes per interpreter, not globally. A tick wave is split by
interpreter: everything that runs Lua on one interpreter is one job, so nodes on different
interpreters tick in parallel and no worker waits for an interpreter. The nodes of one
prefab instance share an interpreter (a big instance is spread over a few), so the scripts
of a prefab call each other directly.

The thread that runs the frame hands every wave to the thread pool and waits for it, so
scripts tick on the workers, like the C++ ticks. It only works on a wave itself when the pool
cannot: when it is a worker itself, when no worker is free, or when none showed up within a
couple of milliseconds. What stays on the frame thread is `physicsTick` (the physics step is
bound to it) and the C++ functions of the lifecycle (see below). The calls that were queued for
the end of a wave are delivered on the pool too, one job per interpreter, in the order they were
made; a handful of them is not worth handing out and runs on the frame thread.

### Physics from scripts

The physics simulator belongs to the thread that ticks the engine. What a script on a worker
asks of it, like creating a node with a rigidbody, enabling or removing one, moving it, setting
its velocity or putting it to sleep, is kept and carried out by the simulator thread at the
start of its next physics step, and once a frame after the ticks, in the order it was asked. So
a body a script has just made exists after the next step and not before, and a velocity set on
it right away is applied when it does. A call that has to answer, a raycast or an overlap, is
answered at once from the state of the last step.

### Building a prefab

The scripts of a prefab are built in two steps. Allocating the nodes and applying their
fields needs no interpreter and runs on every worker. The scripts are then placed in file
order and built one interpreter per job, so the workers never wait for each other, and the
placement does not depend on which worker got there first. A worker only waits for an
interpreter when something outside the prefab, a tick wave of another world, has it. A prefab
that is built from a pool worker or from a script builds its nodes where it is.

### init, begin and onEnable

A tree with many scripts that define the stage (16 or more, on at least two interpreters)
runs `init`, `begin` and `onEnable` level by level, parents before their children. The C++
functions of a level run one after the other on the calling thread, since they register with
the renderer, the physics and the audio and were never written for several threads. Then the
scripts of the level run on the pool, one job per interpreter, and the calls between scripts
that found an interpreter busy are delivered once the level is done. The next level starts
after that.

What this promises is that a node comes after its parent. Two nodes that are not above one
another can run in either order, and at the same time: a script that reads a variable of
another node's script in `init` can find it busy, like in a tick wave. Declare it with
`interactsWith` from the script that needs it, or move the read to `begin`, after `init`
declared it. A smaller tree, or a thread that cannot wait for the pool, runs each node
followed by everything below it, as it always did. Set the environment variable
`TOAST_SERIAL_LIFECYCLE=1` to get that order always, which tells whether a problem comes from
scripts running at the same time.

A script that calls into a node on another interpreter finds that interpreter free, and the
call runs at once, or finds another worker running it. A call that returns nothing (`call`,
signal handlers) is then queued and runs at the end of the wave, in the order it was made.
Reading another node's script variable cannot wait, so it comes back empty with a warning.
When two nodes depend on calling each other right away, declare it from either of them, usually
in `init`:

```lua
function node:init()
	self:interactsWith(self.partner)
end
```

The two nodes then always run in the same job, and the calls between them are always
immediate. It costs parallelism, nodes that interact never run at the same time, so declare
what the scripts really need. A call that had to be queued is logged once with the nodes
involved, which is the hint that a declaration is missing.

Phase dispatch is free for scripts that don't implement the phase: presence is cached in
a bitmask at load, so a node whose script only defines `init` costs nothing per frame.

## Unit tests

`tests/scripting` covers schema extraction (types, groups, ordering, collisions),
scheduling of Lua-only tick functions, hot-reload value preservation, and error
reporting. The reflection generator's stub emission is covered by its own cargo tests.

## Expansion

Sending engine events from scripts (`toast.send`) is the next step once event reflection
matures; the conversion layer it needs already exists. Calls into a busy interpreter that
suspend the calling script and resume it with the result, instead of being queued or
skipped, are the next step for the scheduler.
