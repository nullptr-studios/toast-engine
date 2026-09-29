---@class RayHit
---@field node Node
---@field position vec3
---@field normal vec3
---@field distance number

---@class Physics
Physics = {}

---Damages voxels along a ray, returns whether anything was hit
---@param origin vec3
---@param direction vec3
---@param max_distance number
---@param energy number
---@param min_radius number
---@return boolean
function Physics.shootVoxel(origin, direction, max_distance, energy, min_radius) end
