add_rules("mode.debug", "mode.release")
set_defaultmode("debug")

set_languages("c++23")

rule("clang-format")
	before_build(function(target)
		cprint("${green}[  0%]: ${magenta}<" .. target:name() .. "> ${reset}format")

		local source_files = target:sourcefiles()
		for _, file in ipairs(source_files) do
			if file:endswith(".cpp") then
				os.exec("clang-format -i --style=file:" .. os.curdir() .. "/.clang-format " .. file)
			end
		end

		local header_files = target:headerfiles()
		for _, file in ipairs(header_files) do
			if file:endswith(".hpp") or file:endswith(".h") or file:endswith(".inl") then
				os.exec("clang-format -i --style=file:" .. os.curdir() .. "/.clang-format " .. file)
			end
		end
	end)
rule_end()

add_rules("plugin.compile_commands.autoupdate")
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
	set_optimize("none")
	add_cxxflags("-Og")
	add_defines("DEBUG")
end

includes("engine/xmake.lua")
includes("editor/xmake.lua")
includes("tools/xmake.lua")
includes("tests/xmake.lua")

task("tidy")
set_category("plugin")
on_run(function ()
	local files = os.files("engine/**")
	local target_exts = {
		[".hpp"] = true,
		[".cpp"] = true,
		[".h"]   = true,
		[".inl"] = true
	}

	for _, file in ipairs(files) do
		local ext = path.extension(file)
		if target_exts[ext] then
			cprint("${bright}Tidying: ${reset}" .. file)
			try {
				function ()
					os.execv("clang-tidy", {"-p", ".", file}) 
				end
			}
		end
	end
end)
task_end()
