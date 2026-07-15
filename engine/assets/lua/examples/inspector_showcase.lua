---@class InspectorShowcase : Node
local M = {}

-- plain values
M.alive = true
M.health = 100
M.speed = 4.5
M.title = "hello"

-- math and colors
M.spawn_point = vec3(0, 1, 0)
M.tint = color3(1.0, 0.5, 0.0)

-- references; the marker declares the accepted type before anything is assigned
M.target = Node          -- any node
M.emitter = AudioEmitter -- only AudioEmitter nodes
M.icon = Texture         -- only texture assets

-- arrays are plain lua tables; an empty one needs a marker element to be typed
M.waypoints = { vec3(0, 0, 0), vec3(5, 0, 0) }
M.spawn_names = { "grunt", "brute" }
M.loot = { Asset }

-- groups and subgroups
M.movement = {
	acceleration = 20.0,
	max_speed = 8.0,
	air = {
		control = 0.3,
		gravity_scale = 1.0,
	},
}

M._scratch = 0 -- not exported when prefixed by an underscore

function M:init()
	toast.trace("showcase ready on " .. self:name())
end

return M
