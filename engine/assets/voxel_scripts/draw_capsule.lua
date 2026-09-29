-- A capsule along the longest side of the volume
-- Its radius is half the smallest of the other two sides

---@class DrawCapsule : FillVolume
local M = {}

--- Flat ends instead of round ones
M.cylinder = false

function M:editShape()
    local s = self:getSize()
    local cx, cy, cz = s.x / 2, s.y / 2, s.z / 2
    local a, b, r

    if s.x >= s.y and s.x >= s.z then
        r = math.min(s.y, s.z) / 2
        local inset = self.cylinder and 0 or r
        a, b = vec3(inset, cy, cz), vec3(s.x - inset, cy, cz)
    elseif s.y >= s.z then
        r = math.min(s.x, s.z) / 2
        local inset = self.cylinder and 0 or r
        a, b = vec3(cx, inset, cz), vec3(cx, s.y - inset, cz)
    else
        r = math.min(s.x, s.y) / 2
        local inset = self.cylinder and 0 or r
        a, b = vec3(cx, cy, inset), vec3(cx, cy, s.z - inset)
    end

    if self.cylinder then
        self:fillCylinder(a, b, r, self:getId())
    else
        self:fillCapsule(a, b, r, self:getId())
    end
end

return M
