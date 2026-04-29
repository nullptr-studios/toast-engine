add_rules("mode.debug", "mode.release")
set_defaultmode("debug")

set_languages("c++23")
add_cxxflags("-stdlib=libc++", {tools = {"clang", "gcc"}}) -- Use LLVM STL by default

-- Makes release have flto, fast math and SIMD intrinsic optimizations
if is_mode("release") then
	set_symbols("hidden")
	set_strip("all")
	set_optimize("aggressive")
	set_policy("build.optimization.lto", true)
	add_cxflags("-march=native", "-ffast-math", {force = true, tools = {"clang", "gcc"}})
	add_cxflags("/fp:fast", "/arch:AVX2", {force = true, tools = "cl"})
end

-- Enables debug optimizations, needed on newer clang and gcc versions to avoid a warning
if is_mode("debug") then
	-- add_cxxflags("-Og", "-g", { tools = {"clang", "gcc"} })
	set_optimize("none")
	add_cxxflags("-Og")
end

includes("engine/xmake.lua")
includes("editor/xmake.lua")
includes("tools/xmake.lua")
includes("tests/xmake.lua")
