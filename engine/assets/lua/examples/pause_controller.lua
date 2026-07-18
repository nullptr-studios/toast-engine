---@class PauseController : Panel
-- Handles the pause menu's button events and the two-way vsync checkbox
-- Attach to a Panel node whose element is ui/examples/pause_menu.rml
---@type UIBinds.pause_menu
local M = {}

function M:init()
	-- Give the checkbox a starting value; it round-trips both ways after this
	self.ui_binds.vsync_enabled = true
end

-- Called from `<button data-event-click="Resume()">` with zero C++
function M:Resume()
	toast.info("Resume pressed")
	-- Disabling the panel fires onDisable, which hides the document and drops it
	-- from the draw + input path until it is re-enabled
	self:enabled(false)
	Time.resume()
end

-- Called from `<button data-event-click="Quit()">`
function M:Quit()
	toast.info("Quit pressed")
	-- Wire this to your own shutdown / main-menu flow
end

-- Read the checkbox state that the UI wrote back into the bind store
function M:tick()
	if self.ui_binds.vsync_enabled then
		-- apply vsync-on behavior in your renderer settings here
	end
end

return M
