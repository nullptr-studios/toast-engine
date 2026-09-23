---@class StateTransition
---@field to string
---@field condition? fun(): boolean
---@field invoke? function

---@class State
---@field entry? function
---@field exit? function
---@field tick? function
---@field transitions? StateTransition[] | StateTransition

---@class StateMachine : Node
---@field addState fun(self: StateMachine, name: string, state: State)
