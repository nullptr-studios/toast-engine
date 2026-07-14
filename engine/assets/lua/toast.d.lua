---@meta
-- Core toast API available in every script.

---Logs every argument at info level under the "Lua" sink, tab-separated.
---@param ... any
function print(...) end

---Logs every argument at warning level under the "Lua" sink, tab-separated.
---@param ... any
function warn(...) end

---@class toastlib
toast = {}

---Logs at trace level under the "Lua" sink. Most logs should be traces.
---@param message string
function toast.trace(message) end

---Logs at info level. Use for important state changes, not per-frame chatter.
---@param message string
function toast.info(message) end

---Logs at warning level. Use for potential misuse or unwanted behavior.
---@param message string
function toast.warn(message) end

---Logs at error level. Use when something actually failed.
---@param message string
function toast.error(message) end

---Loads an asset by virtual URI, e.g. "res://materials/steel.mat".
---@param uri string
---@return Asset
function toast.load(uri) end

---Engine time queries; all durations are in seconds.
Time = {}

---Scaled delta time of the current tick.
---@return number
function Time.delta() end

---Unscaled delta time of the current tick.
---@return number
function Time.rawDelta() end

---Delta time of the render loop.
---@return number
function Time.renderDelta() end

---Index of the current frame.
---@return integer
function Time.frame() end

---Seconds since engine start.
---@return number
function Time.uptime() end

---Current frames per second.
---@return number
function Time.fps() end

---Current ticks per second.
---@return number
function Time.tps() end

---Gets or sets the global time scale.
---@param value number?
---@return number
function Time.scale(value) end

---True while the game clock is paused.
---@return boolean
function Time.paused() end

function Time.pause() end

function Time.resume() end

---Tracy profiler hooks; no-ops when profiling is disabled.
tracy = {}

---Opens an unnamed profiling zone; close it with tracy.ZoneEnd().
function tracy.ZoneBegin() end

---Opens a named profiling zone; close it with tracy.ZoneEnd().
---@param name string
function tracy.ZoneBeginN(name) end

function tracy.ZoneEnd() end

---Attaches text to the current zone.
---@param text string
function tracy.ZoneText(text) end

---Emits a standalone profiler message.
---@param text string
function tracy.Message(text) end

---Placeholder declaring a typed Node/Asset reference in an exported variable,
---e.g. `target = Node` or `mesh = Mesh`. The inspector shows a matching picker.
---@class TypeMarker

-- (The generic Node marker and one marker per node type are emitted into types.d.lua
-- by the reflection generator.)

---Marker for a reference to any asset type.
---@type TypeMarker
Asset = nil

-- Asset type markers, one per registered asset type
---@type TypeMarker
Mesh = nil
---@type TypeMarker
Texture = nil
---@type TypeMarker
Data = nil
---@type TypeMarker
Material = nil
---@type TypeMarker
Schema = nil
---@type TypeMarker
Prefab = nil
---@type TypeMarker
Script = nil
---@type TypeMarker
Curve = nil
---@type TypeMarker
AudioStrings = nil
---@type TypeMarker
AudioBank = nil
---@type TypeMarker
Haptic = nil
---@type TypeMarker
Action = nil
---@type TypeMarker
InputLayout = nil
---@type TypeMarker
InputSettings = nil
---@type TypeMarker
AudioEvent = nil
---@type TypeMarker
AudioBus = nil
---@type TypeMarker
AudioPort = nil
---@type TypeMarker
AudioSnapshot = nil
---@type TypeMarker
AudioVca = nil
