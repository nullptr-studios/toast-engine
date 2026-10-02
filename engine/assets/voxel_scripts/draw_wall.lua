-- Tiles a .tvox asset across the whole volume

---@class DrawWall: FillVolume
local M = {}

M.pattern = VoxelModel
M.fit = 0 --- 0 crops, 1 stretches, 2 crops centered
M.anchor = vec3(0, 0, 0) --- 0 min, 1 center, 2 max
M.offset = vec3(0, 0, 0)
M.mirror = vec3(0, 0, 0)

local function hasPattern(self)
    local ok, has = pcall(function () return self.pattern:hasValue() end)
    return ok and has
end

function M:editShape()
    if not hasPattern(self) then
        -- A plain box until a strip is picked so the wall is still visible
        local s = self:getSize()
        self:fillBox(vec3(0, 0, 0), vec3(s.x - 1, s.y - 1, s.z - 1), self:getId())
        return
    end
    self:tile(self.pattern, self.fit, self.anchor, self.offset, self.mirror)
end

return M
