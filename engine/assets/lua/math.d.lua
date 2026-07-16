---@class vec2
---@field x number
---@field y number
---@operator add(vec2): vec2
---@operator sub(vec2): vec2
---@operator mul(number|vec2): vec2
---@operator div(number|vec2): vec2
---@operator unm: vec2
---@operator len: number
local Vec2 = {}

---@param x number
---@param y number
---@return vec2
function vec2(x, y) end

---@return number
function Vec2:length() end

---Squared length
---@return number
function Vec2:length2() end

---@return vec2
function Vec2:normalize() end

---@param other vec2
---@return number
function Vec2:dot(other) end

---@param other vec2
---@return number
function Vec2:distance(other) end

---@param to vec2
---@param t number
---@return vec2
function Vec2:lerp(to, t) end

---@param normal vec2
---@return vec2
function Vec2:reflect(normal) end

---@param onto vec2
---@return vec2
function Vec2:project(onto) end

---Angle to another vector, in radians.
---@param other vec2
---@return number
function Vec2:angle(other) end

---@return vec2
function Vec2:abs() end

---@param lo number|vec2
---@param hi number|vec2
---@return vec2
function Vec2:clamp(lo, hi) end

---@param other vec2
---@return vec2
function Vec2:min(other) end

---@param other vec2
---@return vec2
function Vec2:max(other) end

---@class vec3
---@field x number
---@field y number
---@field z number
---@field r number
---@field g number
---@field b number
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator mul(number|vec3): vec3
---@operator div(number|vec3): vec3
---@operator unm: vec3
---@operator len: number
local Vec3 = {}

---@param x number
---@param y number
---@param z number
---@return vec3
function vec3(x, y, z) end

---@return number
function Vec3:length() end

---Squared length
---@return number
function Vec3:length2() end

---@return vec3
function Vec3:normalize() end

---@param other vec3
---@return number
function Vec3:dot(other) end

---@param other vec3
---@return vec3
function Vec3:cross(other) end

---@param other vec3
---@return number
function Vec3:distance(other) end

---@param to vec3
---@param t number
---@return vec3
function Vec3:lerp(to, t) end

---@param normal vec3
---@return vec3
function Vec3:reflect(normal) end

---@param onto vec3
---@return vec3
function Vec3:project(onto) end

---Angle to another vector, in radians
---@param other vec3
---@return number
function Vec3:angle(other) end

---@return vec3
function Vec3:abs() end

---@param lo number|vec3
---@param hi number|vec3
---@return vec3
function Vec3:clamp(lo, hi) end

---@param other vec3
---@return vec3
function Vec3:min(other) end

---@param other vec3
---@return vec3
function Vec3:max(other) end

---@class vec4
---@field x number
---@field y number
---@field z number
---@field w number
---@field r number
---@field g number
---@field b number
---@field a number
---@operator add(vec4): vec4
---@operator sub(vec4): vec4
---@operator mul(number|vec4): vec4
---@operator div(number|vec4): vec4
---@operator unm: vec4
---@operator len: number
local Vec4 = {}

---@param x number
---@param y number
---@param z number
---@param w number
---@return vec4
function vec4(x, y, z, w) end

---@return number
function Vec4:length() end

---Squared length
---@return number
function Vec4:length2() end

---@return vec4
function Vec4:normalize() end

---@param other vec4
---@return number
function Vec4:dot(other) end

---@param other vec4
---@return number
function Vec4:distance(other) end

---@param to vec4
---@param t number
---@return vec4
function Vec4:lerp(to, t) end

---@return vec4
function Vec4:abs() end

---@param lo number|vec4
---@param hi number|vec4
---@return vec4
function Vec4:clamp(lo, hi) end

---@param other vec4
---@return vec4
function Vec4:min(other) end

---@param other vec4
---@return vec4
function Vec4:max(other) end

---Rotation quaternion
---@class quat
---@field x number
---@field y number
---@field z number
---@field w number
---@operator mul(quat): quat
---@operator mul(vec3): vec3
quat = {}

---@param x number
---@param y number
---@param z number
---@param w number
---@return quat
function quat.new(x, y, z, w) end

---@return quat
function quat.identity() end

---Builds a rotation from euler angles in degrees
---@param degrees vec3
---@return quat
function quat.fromEuler(degrees) end

---Rotation of `degrees` around `axis`.
---@param degrees number
---@param axis vec3
---@return quat
function quat.angleAxis(degrees, axis) end

---Euler angles of this rotation, in degrees
---@return vec3
function quat:toEuler() end

---@return quat
function quat:normalize() end

---@return quat
function quat:inverse() end

---Spherical interpolation towards another rotation
---@param to quat
---@param t number
---@return quat
function quat:slerp(to, t) end

---RGB color, distinct from vec3 so the inspector shows a color picker
---@class color3
---@field r number
---@field g number
---@field b number
local Color3 = {}

---@param r number
---@param g number
---@param b number
---@return color3
function color3(r, g, b) end

---The color as a plain vector for math.
---@return vec3
function Color3:vec3() end

---RGBA color, distinct from vec4 so the inspector shows a color picker
---@class color
---@field r number
---@field g number
---@field b number
---@field a number
local Color4 = {}

---@param r number
---@param g number
---@param b number
---@param a number
---@return color4
function color4(r, g, b, a) end

---The color as a plain vector for math
---@return vec4
function Color4:vec4() end
