---@class Follower : Node3D
local M = {}

M.target = Node -- assign in the inspector, or leave nil and let init() find one
M.lerp_speed = 5.0

function M:init()
	if not self.target then
		-- bare name, slash path ("Level/Player") and node:// URIs all work
		self.target = self:find("Player")
	end

	if self.target then
		-- make sure the target moves before we chase it each frame
		self:addDependsOn(self.target)
	else
		toast.warn(self:name() .. " has no target to follow")
	end
end

function M:tick()
	if not self.target or not self.target:exists() then
		return
	end

	-- reflected fields of other nodes read and write like plain lua values
	local goal = self.target.m_position
	self.m_position = self.m_position:lerp(goal, self.lerp_speed * Time.delta())
end

function M:onDisable()
	-- call() invokes a reflected C++ method
	if self.target and self.target:exists() then
		self.target:call("onFollowerLost")
	end
end

return M
