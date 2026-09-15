---@class Signal
Signal = {}
---@param target Node
---@param function_name string
---@param forwards_args? boolean
---@return boolean
function Signal:connect(target, function_name, forwards_args) end

---@param target Node
---@param function_name string
---@return boolean
function Signal:disconnect(target, function_name) end

function Signal:clear() end

function Signal:fire() end

---@return Signal
function Signal:create() end

---@class Signal0
local Signal0 = {}
---@param target Node
---@param function_name string
---@param forwards_args? boolean
---@return boolean
function Signal0:connect(target, function_name, forwards_args) end

---@param target Node
---@param function_name string
---@return boolean
function Signal0:disconnect(target, function_name) end

function Signal0:clear() end

function Signal0:fire() end

---@class Signal1<T1>
local Signal1 = {}
---@param target Node
---@param function_name string
---@param forwards_args? boolean
---@return boolean
function Signal1:connect(target, function_name, forwards_args) end

---@param target Node
---@param function_name string
---@return boolean
function Signal1:disconnect(target, function_name) end

function Signal1:clear() end

---@param value T1
function Signal1:fire(value) end

---@class Signal2<T1, T2>
local Signal2 = {}
---@param target Node
---@param function_name string
---@param forwards_args? boolean
---@return boolean
function Signal2:connect(target, function_name, forwards_args) end

---@param target Node
---@param function_name string
---@return boolean
function Signal2:disconnect(target, function_name) end

function Signal2:clear() end

---@param value1 T1
---@param value2 T2
function Signal2:fire(value1, value2) end

---@class Signal3<T1, T2, T3>
local Signal3 = {}
---@param target Node
---@param function_name string
---@param forwards_args? boolean
---@return boolean
function Signal3:connect(target, function_name, forwards_args) end

---@param target Node
---@param function_name string
---@return boolean
function Signal3:disconnect(target, function_name) end

function Signal3:clear() end

---@param value1 T1
---@param value2 T2
---@param value3 T3
function Signal3:fire(value1, value2, value3) end

---@class Signal4<T1, T2, T3, T4>
local Signal4 = {}
---@param target Node
---@param function_name string
---@param forwards_args? boolean
---@return boolean
function Signal4:connect(target, function_name, forwards_args) end

---@param target Node
---@param function_name string
---@return boolean
function Signal4:disconnect(target, function_name) end

function Signal4:clear() end

---@param value1 T1
---@param value2 T2
---@param value3 T3
---@param value4 T4
function Signal4:fire(value1, value2, value3, value4) end
