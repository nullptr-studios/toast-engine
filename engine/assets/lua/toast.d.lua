---@meta
-- Core toast API available in every script.

---Logs every argument at info level under the "Lua" sink, tab-separated.
---@param ... any
function print(...) end

---Logs every argument at warning level under the "Lua" sink, tab-separated.
---@param ... any
function warn(...) end


--- Defers execution of callback to end of the frame
---@param fn fun()
function defer(fn) end

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

---UI runtime controls available to every script
UI = {}

---Switches the active UI language and reloads open panel documents so text and
---localized images refresh. Language must be one of the project's [ui] languages.
---@param language string
function UI.setLanguage(language) end

---The active UI language code.
---@return string
function UI.language() end

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
---@class TypeMarker<T>

-- concrete markers come from the engine so their names stay in sync

---Marker for a reference to any asset type.
---@type TypeMarker<Asset>
Asset = nil

---@class Debug
Debug = {}

---@param start vec3
---@param end_ vec3
---@param color? vec4
function Debug.drawLine(start, end_, color) end

---Axis-aligned wireframe box
---@param min vec3
---@param max vec3
---@param color? vec4
function Debug.drawBox(min, max, color) end

---@param center vec3
---@param radius number
---@param color? vec4
---@param segments? integer default 8
function Debug.drawSphere(center, radius, color, segments) end

---@param center vec3
---@param radius number
---@param color? vec4
function Debug.drawSolidSphere(center, radius, color) end

---@param world_position vec3
---@param size number
---@param texture Asset|UID
---@param tint? vec4
function Debug.drawBillboard(world_position, size, texture, tint) end

---@param mesh Asset
---@param transform userdata mat4
---@param tint? vec4
function Debug.drawMesh(mesh, transform, tint) end

---@param from vec3
---@param to vec3
---@param color? vec4
---@param head_size? number default 0.2
function Debug.drawArrow(from, to, color, head_size) end

---@param apex vec3
---@param direction vec3
---@param length number
---@param half_angle_degrees number
---@param color? vec4
---@param segments? integer default 24
function Debug.drawCone(apex, direction, length, half_angle_degrees, color, segments) end

---@param camera Camera
---@param aspect number
---@param color? vec4 default yellow
---@param far_override? number 0 uses the camera's far plane
function Debug.drawFrustum(camera, aspect, color, far_override) end

---@param transform userdata mat4
---@param color vec4
---@param fill boolean
function Debug.drawShapeBox(transform, color, fill) end

---@param transform userdata mat4
---@param radius number
---@param height number
---@param color vec4
---@param fill boolean
function Debug.drawCapsule(transform, radius, height, color, fill) end
