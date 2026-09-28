-- Fills the whole volume, you can round the corners

---@class DrawBox: FillVolume
local M = {}

M.cornerRadius = 0.0

function M:editShape()
    local s = self:getSize()
    self:fillRoundBox(vec3(0, 0, 0), vec3(s.x - 1, s.y - 1, s.z - 1), self.cornerRadius, self:getId())
end

return M
