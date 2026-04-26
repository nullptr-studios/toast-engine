add_rules("mode.debug", "mode.release")
set_defaultmode("debug")

set_languages("c++23")

rule("clang-format")
	before_build(function(target)
		cprint("${green}[  0%]: ${magenta}<" .. target:name() .. "> ${reset}format")
		
		local files = target:sourcefiles()
		for _, file in ipairs(files) do
			if file:endswith(".cpp") or file:endswith(".hpp") or file:endswith(".h") or file:endswith(".inl") then
				os.exec("clang-format -i --style=file:" .. os.curdir() .. "/.clang-format " .. file)
			end
		end
	end)
rule_end()
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
