---@meta
-- Node and Asset proxies
--
--   ---@class PlayerScript : Node3D
--   local M = {}
--
--   M.speed = 4.0
--
--   function M:tick()
--       self.m_position = self.m_position + vec3(0, 0, self.speed * Time.delta())
--   end
--
--   return M

---@class Node
local NodeProxy = {}

---True while the referenced node is alive.
---@return boolean
function NodeProxy:exists() end

---@return string
function NodeProxy:name() end

---@return integer
function NodeProxy:uid() end

---@param query string
---@return Node?
function NodeProxy:find(query) end

---@param query string
---@return Node[]
function NodeProxy:search(query) end

---@param type string?
---@return Node?
function NodeProxy:create(type) end

---@param other Node
function NodeProxy:addDependsOn(other) end

---@param method string
---@param ... any
---@return any
function NodeProxy:call(method, ...) end

---@param value boolean?
---@return boolean?
function NodeProxy:enabled(value) end

---@class Asset
local AssetProxy = {}

---@return string
function AssetProxy:path() end

---@return integer
function AssetProxy:uid() end

---@return boolean
function AssetProxy:hasValue() end

---@return string
function AssetProxy:type() end
