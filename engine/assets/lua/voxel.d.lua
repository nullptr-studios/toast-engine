---@meta
-- Voxel shapes
--
-- A VoxelNode is a shape made of voxels that renders and collides
-- It starts from a voxel model or from nothing and editShape can change it however it wants
-- Positions are in voxels relative to the node and each voxel is 10 cm
-- Ids are palette colors and 0 means empty
--
--   ---@class Crater : VoxelNode
--   local M = {}
--
--   function M:editShape()
--       self:carveSphere(vec3(0, 0, 8), 6)
--   end
--
--   return M

--- Picks which voxels a fill stamp or paste is allowed to overwrite
---@enum VoxelWriteValue
VoxelWrite = {
    --- Overwrites anything
    Replace = 0,
    --- Only writes into empty voxels
    EmptyOnly = 1,
    --- Only overwrites solid voxels so it never adds any
    SolidOnly = 2,
    --- Only overwrites voxels holding match_id
    Match = 3,
}

---@class VoxelNode
---@field rebuilt_shape Signal0 Sent after a rebuild once editShape is done
---@field damaged Signal2<integer, vec3> Sent when hits remove voxels with how many and where in the world
---@field broke_apart Signal1<integer> Sent when a piece breaks off with how many voxels it took
---@field emptied Signal0 Sent when the last voxel is gone
---@field contact_begin Signal1<Node> Sent when it starts touching another body
---@field contact_end Signal1<Node> Sent when it stops touching another body
---@field went_to_sleep Signal0 Sent when the body falls asleep
---@field woke_up Signal0 Sent when the body wakes up
local VoxelNodeProxy = {}

-- Where the shape comes from

--- The voxel model the shape starts from
---@return Asset
function VoxelNodeProxy:getModel() end

--- Swaps the model and rebuilds the shape
---@param model Asset
function VoxelNodeProxy:setModel(model) end

--- The palette that replaces the model colors
---@return Asset
function VoxelNodeProxy:getPalette() end

--- Replaces the colors the model came with
---@param palette Asset
function VoxelNodeProxy:setPalette(palette) end

--- The palette in use as a uid
--- The override first then the model palette then 0
---@return integer
function VoxelNodeProxy:paletteUid() end

-- Building the shape

--- Write this in your script to build the shape
--- It runs every time the shape is rebuilt
--- Rebuilds happen on init and when the model or the scripts change
function VoxelNodeProxy:editShape() end

-- Reading voxels

--- The color id at pos or 0 if it is empty
---@param pos vec3
---@return integer
function VoxelNodeProxy:getVoxel(pos) end

--- The size of the shape in voxels
---@return vec3
function VoxelNodeProxy:getSize() end

--- The lowest and highest solid voxel
---@return vec3[] bounds Min then max or an empty list when there are no voxels
function VoxelNodeProxy:getBounds() end

--- How many solid voxels there are
---@return integer
function VoxelNodeProxy:getVoxelCount() end

--- Turns a world position into a voxel position
---@param pos vec3
---@return vec3
function VoxelNodeProxy:worldToVoxel(pos) end

--- Turns a voxel position into a world position
---@param pos vec3
---@return vec3
function VoxelNodeProxy:voxelToWorld(pos) end

-- Writing voxels
-- Adding voxels grows the shape on its own
-- Editing another node needs a tick dependency on it

--- Sets the voxel at pos to id
---@param pos vec3
---@param id integer
function VoxelNodeProxy:setVoxel(pos, id) end

--- Empties the voxel at pos
---@param pos vec3
function VoxelNodeProxy:removeVoxel(pos) end

--- Fills the box from min to max with id
---@param min vec3
---@param max vec3
---@param id integer
---@param mode? VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer The only id that Match overwrites
function VoxelNodeProxy:fillBox(min, max, id, mode, match_id) end

--- Fills a ball around center with id
---@param center vec3
---@param radius number
---@param id integer
---@param mode? VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer The only id that Match overwrites
function VoxelNodeProxy:fillSphere(center, radius, id, mode, match_id) end

--- Fills a flat ended cylinder from a to b with id
---@param a vec3
---@param b vec3
---@param radius number
---@param id integer
---@param mode? VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer The only id that Match overwrites
function VoxelNodeProxy:fillCylinder(a, b, radius, id, mode, match_id) end

--- Fills a round ended tube from a to b with id
---@param a vec3
---@param b vec3
---@param radius number
---@param id integer
---@param mode? VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer The only id that Match overwrites
function VoxelNodeProxy:fillLine(a, b, radius, id, mode, match_id) end

--- Empties the box from min to max
---@param min vec3
---@param max vec3
function VoxelNodeProxy:carveBox(min, max) end

--- Empties a ball around center
---@param center vec3
---@param radius number
function VoxelNodeProxy:carveSphere(center, radius) end

--- Empties a flat ended cylinder from a to b
---@param a vec3
---@param b vec3
---@param radius number
function VoxelNodeProxy:carveCylinder(a, b, radius) end

--- Empties a round ended tube from a to b
---@param a vec3
---@param b vec3
---@param radius number
function VoxelNodeProxy:carveLine(a, b, radius) end

--- Recolors the solid voxels from min to max
--- Never adds voxels
---@param min vec3
---@param max vec3
---@param id integer
function VoxelNodeProxy:paintBox(min, max, id) end

--- Recolors the solid voxels in a ball
--- Never adds voxels
---@param center vec3
---@param radius number
---@param id integer
function VoxelNodeProxy:paintSphere(center, radius, id) end

--- Turns every voxel of one id into another
---@param from integer
---@param to integer
function VoxelNodeProxy:replaceId(from, to) end

--- Cuts off everything on one side of a plane
---@param point vec3 Any point on the plane
---@param normal vec3
---@param side integer 1 cuts where normal points and minus 1 cuts the other side
function VoxelNodeProxy:slice(point, normal, side) end

--- Pulls the flat face at pos out along normal
---@param pos vec3 A voxel on the face
---@param normal vec3 Snaps to the closest axis
---@param distance integer How many voxels to move it and negative pushes it in
function VoxelNodeProxy:extrudeFace(pos, normal, distance) end

--- Drops a voxel model into the shape at pos
--- Colors get matched to this palette
---@param asset Asset A voxel model
---@param pos vec3
---@param rotation? quat Snaps to the closest right angle
---@param mode? VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer The only id that Match overwrites
function VoxelNodeProxy:stamp(asset, pos, rotation, mode, match_id) end

--- Copies the voxels from min to max
---@param min vec3
---@param max vec3
---@return integer region A number to hand to paste
function VoxelNodeProxy:copy(min, max) end

--- Pastes a copied region with its corner at pos
--- Empty cells are skipped so it never erases
---@param region integer What copy gave back
---@param pos vec3
---@param mode? VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer The only id that Match overwrites
function VoxelNodeProxy:paste(region, pos, mode, match_id) end

--- Forgets every copied region
function VoxelNodeProxy:clearRegions() end

-- Physics

--- Puts the physics body to sleep
function VoxelNodeProxy:sleep() end

--- Wakes the physics body up
function VoxelNodeProxy:wake() end

--- A sphere around the shape
---@return vec4 sphere Center in xyz and radius in w
function VoxelNodeProxy:localBoundingSphere() end
