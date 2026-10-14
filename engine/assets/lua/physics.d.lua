---@class RayHit
---@field node Node
---@field position vec3
---@field normal vec3
---@field distance number

---@class Physics
Physics = {}

---Area damage where the ray first hits voxels
---@param origin vec3
---@param direction vec3
---@param max_distance number
---@param energy_density number kJ/m³
---@param fracture_radius number Meters broken around the hit
---@return boolean
function Physics.shootVoxel(origin, direction, max_distance, energy_density, fracture_radius) end

---Breaks the voxels within radius of the point the energy can pay for
---@param point vec3
---@param radius number Fracture radius in metres
---@param energy_density number kJ/m³
---@param direction? vec3
---@param hardness? "soft"|"medium"|"hard" Hardest tier it breaks
function Physics.damageArea(point, radius, energy_density, direction, hardness) end

---Ekusuploooooooshiooooooooon (megumin, probably)
---@param point vec3
---@param force number kJ/m³
---@param falloff? number 2 is the inverse square
---@param hardness? "soft"|"medium"|"hard" Hardest tier it breaks
---@return integer voxels
function Physics.explode(point, force, falloff, hardness) end

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
