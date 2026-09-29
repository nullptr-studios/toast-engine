-- This version writes each voxel in Lua instead of using fillEllipsoid to
-- test performance for heavilly dynamic voxel scripts we will have on the
-- future

---@class DrawSphereNative: FillVolume
local M = {}

function M:editShape()
    local s = self:getSize()
    local hx, hy, hz = s.x / 2, s.y / 2, s.z / 2
    local id = self:getId()

    for z = 0, s.z - 1 do
        local nz = (z + 0.5 - hz) / hz
        local zz = nz * nz
        for y = 0, s.y - 1 do
            local ny = (y + 0.5 - hy) / hy
            local yyzz = ny * ny + zz
            for x = 0, s.x - 1 do
                local nx = (x + 0.5 - hx) / hx
                if nx * nx + yyzz <= 1 then
                    self:setVoxel(vec3(x, y, z), id)
                end
            end
        end
    end
end

return M
