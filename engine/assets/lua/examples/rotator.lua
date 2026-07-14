---@class Rotator : Node3D
local M = {}

M.degrees_per_second = 45.0

function M:tick()
	local spin = quat.angleAxis(self.degrees_per_second * Time.delta(), vec3(0, 1, 0))
	self.rotation = spin * self.rotation
end

return M
