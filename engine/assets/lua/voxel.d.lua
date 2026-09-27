--- Picks which voxels a fill stamp or paste is allowed to overwrite
---@enum VoxelWriteValue
VoxelWrite = {
    --- Overwrites anything
    Replace = 0,
    --- Only writes into empty voxels
    EmptyOnly = 1,
    --- Only overwrites solid voxels
    SolidOnly = 2,
    --- Only overwrites voxels holding match_id
    Match = 3
}

---@class VoxelNode
---@field rebuilt_shape Signal0                Sent after a rebuild once editShape is done
---@field damaged       Signal2<integer, vec3> Sent when hits remove voxels with how many and where in the world
---@field broke_apart   Signal1<integer>       Sent when a piece breaks off with how many voxels it took
---@field emptied       Signal0                Sent when the last voxel is gone
---@field contact_begin Signal1<Node>          Sent when it starts touching another body
---@field contact_end   Signal1<Node>          Sent when it stops touching another body
---@field went_to_sleep Signal0                Sent when the body falls asleep
---@field woke_up       Signal0                Sent when the body wakes up
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
---@param id  integer
function VoxelNodeProxy:setVoxel(pos, id) end

--- Empties the voxel at pos
---@param pos vec3
function VoxelNodeProxy:removeVoxel(pos) end

--- Fills the box from min to max with id
---@param min       vec3
---@param max       vec3
---@param id        integer
---@param mode?     VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer         The only id that Match overwrites
function VoxelNodeProxy:fillBox(min, max, id, mode, match_id) end

--- Fills a ball around center with id
---@param center    vec3
---@param radius    number
---@param id        integer
---@param mode?     VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer         The only id that Match overwrites
function VoxelNodeProxy:fillSphere(center, radius, id, mode, match_id) end

--- Fills a flat ended cylinder from a to b with id
---@param a         vec3
---@param b         vec3
---@param radius    number
---@param id        integer
---@param mode?     VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer         The only id that Match overwrites
function VoxelNodeProxy:fillCylinder(a, b, radius, id, mode, match_id) end

--- Fills a round ended tube from a to b with id
---@param a         vec3
---@param b         vec3
---@param radius    number
---@param id        integer
---@param mode?     VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer         The only id that Match overwrites
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
---@param a      vec3
---@param b      vec3
---@param radius number
function VoxelNodeProxy:carveCylinder(a, b, radius) end

--- Empties a round ended tube from a to b
---@param a      vec3
---@param b      vec3
---@param radius number
function VoxelNodeProxy:carveLine(a, b, radius) end

--- Recolors the solid voxels from min to max
--- Never adds voxels
---@param min vec3
---@param max vec3
---@param id  integer
function VoxelNodeProxy:paintBox(min, max, id) end

--- Recolors the solid voxels in a ball
--- Never adds voxels
---@param center vec3
---@param radius number
---@param id     integer
function VoxelNodeProxy:paintSphere(center, radius, id) end

--- Turns every voxel of one id into another
---@param from integer
---@param to   integer
function VoxelNodeProxy:replaceId(from, to) end

--- Cuts off everything on one side of a plane
---@param point  vec3    Any point on the plane
---@param normal vec3
---@param side   integer 1 cuts where normal points and minus 1 cuts the other side
function VoxelNodeProxy:slice(point, normal, side) end

--- Pulls the flat face at pos out along normal
---@param pos      vec3    A voxel on the face
---@param normal   vec3    Snaps to the closest axis
---@param distance integer How many voxels to move it and negative pushes it in
function VoxelNodeProxy:extrudeFace(pos, normal, distance) end

--- Drops a voxel model into the shape at pos
--- Colors get matched to this palette
---@param asset     Asset           A voxel model
---@param pos       vec3
---@param rotation? quat            Snaps to the closest right angle
---@param mode?     VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer         The only id that Match overwrites
function VoxelNodeProxy:stamp(asset, pos, rotation, mode, match_id) end

--- Copies the voxels from min to max
---@param min vec3
---@param max vec3
---@return integer region A number to hand to paste
function VoxelNodeProxy:copy(min, max) end

--- Pastes a copied region with its corner at pos
--- Empty cells are skipped so it never erases
---@param region    integer         What copy gave back
---@param pos       vec3
---@param mode?     VoxelWriteValue What it is allowed to overwrite and Replace when left out
---@param match_id? integer         The only id that Match overwrites
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

-- Procedural voxel pieces
--
-- FillVolume, CarveVolume, PaintVolume and VoxelMesh are pieces of a ProceduralVoxel
-- Each one draws into its own grid with editShape and the ProceduralVoxel applies them top to bottom
-- Positions are in voxels from the piece corner and nothing can be drawn past getSize()
--
--   ---@class Arch : FillVolume
--   local M = {}
--   M.thickness = 2
--
--   function M:editShape()
--       local s = self:getSize()
--       self:fillBox(vec3(0, 0, 0), vec3(s.x - 1, s.y - 1, s.z - 1), self:getId())
--       self:carveBox(vec3(self.thickness, 0, 0), vec3(s.x - 1 - self.thickness, s.y - 1, s.z - 1 - self.thickness))
--   end
--
--   return M

---@class VoxelPiece
---@field drawn Signal0 Sent after editShape drew the grid again
local VoxelPieceProxy = {}

--- Write this in your script to draw the piece
--- It only runs again when the size, color, script or its variables change
function VoxelPieceProxy:editShape() end

--- The size scripts draw into in voxels
---@return vec3
function VoxelPieceProxy:getSize() end

--- The color id at pos or 0 if it is empty
---@param pos vec3
---@return integer
function VoxelPieceProxy:getVoxel(pos) end

---@param pos vec3
---@param id  integer
function VoxelPieceProxy:setVoxel(pos, id) end

---@param pos vec3
function VoxelPieceProxy:removeVoxel(pos) end

---@param min       vec3
---@param max       vec3
---@param id        integer
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:fillBox(min, max, id, mode, match_id) end

--- A box with its edges rounded by radius voxels
---@param min       vec3
---@param max       vec3
---@param radius    number
---@param id        integer
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:fillRoundBox(min, max, radius, id, mode, match_id) end

---@param center    vec3
---@param radius    number
---@param id        integer
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:fillSphere(center, radius, id, mode, match_id) end

--- The ellipsoid that touches every face of the box from min to max
---@param min       vec3
---@param max       vec3
---@param id        integer
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:fillEllipsoid(min, max, id, mode, match_id) end

--- A flat ended cylinder from a to b
---@param a         vec3
---@param b         vec3
---@param radius    number
---@param id        integer
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:fillCylinder(a, b, radius, id, mode, match_id) end

--- A round ended tube from a to b
---@param a         vec3
---@param b         vec3
---@param radius    number
---@param id        integer
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:fillCapsule(a, b, radius, id, mode, match_id) end

---@param min vec3
---@param max vec3
function VoxelPieceProxy:carveBox(min, max) end

---@param center vec3
---@param radius number
function VoxelPieceProxy:carveSphere(center, radius) end

--- Recolors the solid voxels from min to max
---@param min vec3
---@param max vec3
---@param id  integer
function VoxelPieceProxy:paintBox(min, max, id) end

--- Drops a voxel model into the grid at pos
---@param asset     Asset           A voxel model
---@param pos       vec3
---@param rotation? quat            Snaps to the closest right angle
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:stamp(asset, pos, rotation, mode, match_id) end

--- Repeats the solid part of a voxel model across the whole grid
---@param asset     Asset           A voxel model
---@param fit?      integer         0 crop, 1 stretch, 2 center
---@param anchor?   vec3            Per axis 0 min, 1 center, 2 max
---@param offset?   vec3            Shifts the tiles by this many voxels
---@param mirror?   vec3            Per axis, non zero flips every other tile
---@param mode?     VoxelWriteValue
---@param match_id? integer
function VoxelPieceProxy:tile(asset, fit, anchor, offset, mirror, mode, match_id) end

-- Piece

--- Runs editShape again on the next rebuild
--- Only needed when the script reads something the piece cannot see change
function VoxelPieceProxy:redraw() end

--- The script that draws the piece
---@return Asset
function VoxelPieceProxy:getShapeScript() end

--- Swaps the script that draws the piece and redraws it
---@param script Asset
function VoxelPieceProxy:setShapeScript(script) end

--- Clip planes in piece voxels, a voxel is kept when dot(xyz, centre) + w >= 0
---@return vec4[]
function VoxelPieceProxy:getClipPlanes() end

---@param planes vec4[]
function VoxelPieceProxy:setClipPlanes(planes) end

--- Keeps only the voxels on the side the normal in xyz points to
---@param plane vec4
function VoxelPieceProxy:addClipPlane(plane) end

function VoxelPieceProxy:clearClipPlanes() end

--- The box the piece covers on its ProceduralVoxel in shape voxels
---@return vec3[] bounds Min then max or an empty list when it is not in a ProceduralVoxel
function VoxelPieceProxy:getShapeBounds() end

--- The ProceduralVoxel the piece is built into, through any groups
---@return Node?
function VoxelPieceProxy:getShape() end

--- True when the transform had to be snapped to the closest voxel and right angle
---@return boolean
function VoxelPieceProxy:isMisaligned() end

---@class VoxelVolume: VoxelPiece
local VoxelVolumeProxy = {}

--- The color the volume draws with
---@return integer
function VoxelVolumeProxy:getId() end

--- Changes the color the volume draws with and redraws it
---@param id integer
function VoxelVolumeProxy:setId(id) end

--- Resizes the volume in voxels, every axis is at least 1
---@param size vec3
function VoxelVolumeProxy:setSize(size) end

--- What the volume is allowed to overwrite
---@return VoxelWriteValue
function VoxelVolumeProxy:getMode() end

---@param mode VoxelWriteValue
function VoxelVolumeProxy:setMode(mode) end

--- The only id that Match overwrites
---@return integer
function VoxelVolumeProxy:getMatchId() end

---@param match_id integer
function VoxelVolumeProxy:setMatchId(match_id) end

---@class VoxelMesh: VoxelPiece
local VoxelMeshProxy = {}

--- The voxel model the piece draws
---@return Asset
function VoxelMeshProxy:getModel() end

--- Swaps the model and redraws the piece
---@param model Asset
function VoxelMeshProxy:setModel(model) end

--- The palette that replaces the model colors
---@return Asset
function VoxelMeshProxy:getPalette() end

---@param palette Asset
function VoxelMeshProxy:setPalette(palette) end

---@class VoxelBucket
local VoxelBucketProxy = {}

--- The color it paints with
---@return integer
function VoxelBucketProxy:getId() end

---@param id integer
function VoxelBucketProxy:setId(id) end

--- The ProceduralVoxel the bucket paints, through any groups
---@return Node?
function VoxelBucketProxy:getShape() end

---@class VoxelGroup
local VoxelGroupProxy = {}

--- The ProceduralVoxel the group is built into, through any groups
---@return Node?
function VoxelGroupProxy:getShape() end

---@class ProceduralVoxel: VoxelNode
local ProceduralVoxelProxy = {}

--- Rebuilds the shape from its pieces now
function ProceduralVoxelProxy:rebuild() end

--- The piece whose color shows at a voxel of the shape
--- In game the pieces redraw their grid the first time this reads them
---@param pos vec3
---@return Node?
function ProceduralVoxelProxy:getPieceAt(pos) end

--- How many pieces the last rebuild used
---@return integer
function ProceduralVoxelProxy:getPieceCount() end
