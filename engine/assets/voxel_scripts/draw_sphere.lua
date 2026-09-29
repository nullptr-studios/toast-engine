-- The ellipsoid that touches every face of the volume
-- A cube gives a sphere and any other size stretches it

---@class DrawSphere : FillVolume
local M = {}

function M:editShape()
    local s = self:getSize()
    self:fillEllipsoid(vec3(0, 0, 0), vec3(s.x - 1, s.y - 1, s.z - 1), self:getId())
end

return M
