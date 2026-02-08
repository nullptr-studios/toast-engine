-- Generates a bin path following the default build/p/a/m structure
local get_path = function()
	local host = get_config("plat")
	local arch = get_config("arch")
	local mode = get_config("mode")
	return path.join("build", host, arch, mode)
end

target("toast.editor", function()
	set_kind("phony")

	on_build(function(target)
		cprint("${cyan bright}[toast.editor] building...")

		local args = {
			"publish",
			"editor/editor.csproj",
			"-c", (mode == "debug") and "Debug" or "Release",
			"-o", get_path()
		}
		os.execv("dotnet", args)
	end)

	on_run(function()
		if is_host("windows") then
			os.exec(path.join(get_path(), "editor.exe"))
		else
			os.exec(path.join(get_path(), "editor"))
		end
	end)
end)
