---@class HudController : Panel
-- Drives the HUD panel's data binds every tick and toggles the language on a key press
-- Attach to a Panel node whose element is ui/examples/hud.rml
---@type UIBinds.hud
local M = {}

-- Fake gameplay state so the example animates on its own
M.health = 100.0
M.ammo = 42
M.m_language_index = 1

local languages = { "en", "es" }

function M:init()
	-- Seed the binds so the first frame shows real values
	self.ui_binds.health = "100%"
	self.ui_binds.ammo = self.ammo
end

function M:tick()
	-- Drain and regenerate health to show the bound bar move
	self.health = self.health - 8.0 * Time.delta()
	if self.health < 0 then
		self.health = 100.0
	end

	-- Push gameplay state into the document through the two-way bind store
	self.ui_binds.health = string.format("%d%%", math.floor(self.health))
	self.ui_binds.ammo = self.ammo
end

-- Bound in the demo scene to a key action; flips the UI language at runtime
-- Every open document re-translates and re-applies localized images
function M:CycleLanguage()
	self.m_language_index = self.m_language_index % #languages + 1
	UI.setLanguage(languages[self.m_language_index])
	toast.info("UI language -> " .. languages[self.m_language_index])
end

return M
