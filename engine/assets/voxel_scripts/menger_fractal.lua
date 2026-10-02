-- A Menger sponge stretched to the volume, coded it as a demo for voxel scripts

---@class MengerSponge: FillVolume
local M = {}

M.depth = 3 -- every level is 20x more voxels

local function cut(from, to, third)
    return math.floor(from + (to - from) * third / 3 + 0.5)
end

local function carve(self, lo, hi, depth)
    if depth == 0 or hi.x - lo.x < 3 or hi.y - lo.y < 3 or hi.z - lo.z < 3 then
        return
    end
    for i = 0, 2 do
        for j = 0, 2 do
            for k = 0, 2 do
                local a = vec3(cut(lo.x, hi.x, i), cut(lo.y, hi.y, j), cut(lo.z, hi.z, k))
                local b = vec3(cut(lo.x, hi.x, i + 1), cut(lo.y, hi.y, j + 1), cut(lo.z, hi.z, k + 1))
                local centres = (i == 1 and 1 or 0) + (j == 1 and 1 or 0) + (k == 1 and 1 or 0)
                if centres >= 2 then
                    self:carveBox(a, vec3(b.x - 1, b.y - 1, b.z - 1))
                else
                    carve(self, a, b, depth - 1)
                end
            end
        end
    end
end

function M:editShape()
    local s = self:getSize()
    self:fillBox(vec3(0, 0, 0), vec3(s.x - 1, s.y - 1, s.z - 1), self:getId())
    carve(self, vec3(0, 0, 0), s, self.depth)
end

return M
