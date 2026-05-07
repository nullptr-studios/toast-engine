add_rules("mode.debug", "mode.release")
set_defaultmode("debug")

set_languages("c++latest")

rule("clang-format")
before_build(function(target)
	cprint("${green}[100%]: ${magenta}<" .. target:name() .. "> ${reset}format")

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
-- add_syslinks("stdc++exp", { tools = { "clang", "gcc" } }) -- adds library for stacktrace
if is_plat("linux", "macosx") then
    add_syslinks("stdc++exp")
end

-- Makes release have flto, fast math and SIMD intrinsic optimizations
if is_mode("release") then
	set_symbols("hidden")
	set_strip("all")
	set_optimize("aggressive")
	set_policy("build.optimization.lto", true)
	add_cxflags("-march=native", "-ffast-math", { force = true, tools = { "clang", "gcc" } })
	add_cxflags("/fp:fast", "/arch:AVX2", { force = true, tools = "cl" })
end

-- Enables debug optimizations, needed on newer clang and gcc versions to avoid a warning
if is_mode("debug") then
	set_optimize("none")
	add_cxxflags("-Og", { tools = { "clang", "gcc" } }) -- this line doesnt quite work
	add_defines("DEBUG")
end

includes("engine/xmake.lua")
includes("editor/xmake.lua")
includes("tools/xmake.lua")
includes("tests/xmake.lua")

task("tidy")
set_menu {
	usage = "xmake tidy",
	description = "Run clang-tidy on engine files with progress"
}
on_run(function()
	local all_files = os.files("engine/**")
	local targets = {}
	for _, file in ipairs(all_files) do
		local ext = path.extension(file)
		local is_valid_ext = (ext == ".hpp" or ext == ".cpp" or ext == ".h" or ext == ".inl")

		-- Skip generated and external folders
		if is_valid_ext and
				not file:find("engine/generated", 1, true) and
				not file:find("engine/external", 1, true) and
				not file:find("engine/ffi", 1, true) then
			table.insert(targets, file)
		end
	end

	local total = #targets
	if total == 0 then
		cprint("${yellow}No files found to analyze.")
		return
	end

	for i, file in ipairs(targets) do
		local percentage = math.floor((i / total) * 100)
		local progress = string.format("%3d%%", percentage)
		cprint("${green}[" .. progress .. "]: ${reset}clang-tidy.analyzing " .. file)
		try {
			function()
				os.execv("clang-tidy", { "-p", ".", file })
			end
		}
	end
end)
task_end()
