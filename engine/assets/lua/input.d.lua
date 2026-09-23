---@enum InputActionEvent
InputEvent = {
    start = 0,
    hold = 1,
    release = 2,
    tries = 3,
    countdown = 4,
    cancelled = 5,
}

---@enum InputDeviceValue
InputDevice = {
    none = 0,
    keyboard = 1,
    mouse = 2,
    controller = 3,
}

---@enum InputValueTypeValue
InputValueType = {
    axis0d = 0,
    axis1d = 1,
    axis2d = 2,
}

---@enum InputModifierValue
InputModifier = {
    none = 0,
    shift = 1,
    control = 2,
    alt = 4,
}

---@enum InputKindValue
InputKind = {
    button = 0,
    axis1d = 1,
    axis2d = 2,
    scroll = 3,
    cursor = 4,
}

---@class InputKeyCode
---@field device InputDeviceValue
---@field kind InputKindValue
---@field code integer
---@field valid boolean
local InputKeyCode = {}

---@class InputBind
local InputBind = {}

---@return InputKeyCode
function InputBind:keycode() end

---@return string
function InputBind:keycodeString() end

---@class InputAction
local InputAction = {}

---@return integer
function InputAction:uid() end

---@return string
function InputAction:name() end

---@return string
function InputAction:functionName() end

---@return InputValueTypeValue
function InputAction:valueType() end

---@return boolean|number|vec2
function InputAction:value() end

---@return InputModifierValue
function InputAction:modifiers() end

---@return InputDeviceValue
function InputAction:device() end

---@return number
function InputAction:timeSinceStart() end

---@return number
function InputAction:timeSinceTry() end

---@return number
function InputAction:remainingCountdown() end

---@return InputBind[]
function InputAction:binds() end

---@alias InputHandler fun(self: Node, action: InputAction, event: InputActionEvent)
