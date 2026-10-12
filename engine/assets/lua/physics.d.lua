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

---Sets the wind of the level in world space m/s
---It lasts until another level is loaded or Physics.resetWind is called
---@param wind vec3
function Physics.setWind(wind) end

---The wind the physics currently uses in world space m/s
---@return vec3
function Physics.wind() end

---Goes back to the wind of the physics settings
function Physics.resetWind() end

---Index of the physics layer with this name
---@param name string
---@return integer
function Physics.getLayer(name) end
