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

---@return string [Display name. Siblings may share one - this is not an identifier]
function NodeProxy:name() end

---@return integer [Stable unique identifier, assigned at construction or deserialization. Never changes]
function NodeProxy:uid() end

---Finds the first descendant whose name or path matches `query`.
---Searches Depth-first with the children of this node
---traversal stops at prefab-interface boundaries
---These prefixes will change the starting location of the find `root/`, `world/`, `global`
---@param query string
---@return Node? [The first match, or an empty box if nothing was found]
function NodeProxy:find(query) end

---Finds the every descendant whose name or path matches `query`.
---Searches Depth-first with the children of this node
---Unlike find(), search() crosses prefab-instance boundaries
---These prefixes will change the starting location of the find `root/`, `world/`, `global`
---@param query string
---@return Node[] [All matches in depth-first order; may be empty]
function NodeProxy:search(query) end

---Creates a new child node of the given type and attaches it to this node
---Only valid on nodes in the root or global state; calls the child's init lifecycle
---@param type string? [C++ class name, e.g. "toast::Node3D"; defaults to "toast::Node"]
---@return Node? [The new child node, or an empty box if the type is not registered]
function NodeProxy:create(type) end

--- probably shouldnt use in lua - dante
---@param other Node
function NodeProxy:addDependsOn(other) end

---Invokes all C++ reflected implementations of function `name` (base→derived) and
---all same-named Lua functions across every attached script, forwarding `args`
---@param method string [Name of the function]
---@param ... any [Arguments forwarded to the function parameters]
---@return any [R Return type, for non-void the most-derived C++ return value is returned]
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
