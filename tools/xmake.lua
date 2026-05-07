add_requires("cargo::anyhow 1.0")
add_requires("cargo::chrono 0.4")
add_requires("cargo::clap", { configs = { features = "derive" } })
add_requires("cargo::csv 1.3.1")
add_requires("cargo::crossterm 0.28.1")
add_requires("cargo::prost 0.14")
add_requires("cargo::prost-types 0.13.5")
add_requires("cargo::serde", { configs = { features = "derive" } })
add_requires("cargo::textwrap 0.16")
add_requires("cargo::tokio", { configs = { features = "full" } })
add_requires("cargo::tui-input 0.11")

local tools_dir = os.scriptdir()

target("player", function()
	set_kind("binary")
	set_default(false)
	add_deps("toast.engine", "toast.dummy_game")
	add_files("player/**.cs")
	remove_files("player/obj/**")
	set_values("csharp.target_framework", "net10.0")
	set_values("csharp.nullable", "enable")
	set_values("csharp.implicit_usings", "enable")
	set_values("csharp.generate_assembly_info", "false")
end)

target("toast.dummy_game", function()
	set_kind("shared")
	set_default(false)
	add_files("dummy_game/**.cpp")
	add_headerfiles("dummy_game/**.h")
	add_deps("toast.engine")
	add_defines("GAME_EXPORT")
	add_rpathdirs("./")
end)

target("kenzo", function()
	set_kind("binary")
	set_default(false)
	on_build(function(target)
		local oldir = os.cd("$(projectdir)/tools/kenzo")

		if is_mode("release") then
			os.execv("cargo", { "build", "--release" })
		else
			os.execv("cargo", { "build" })
		end

		-- Copy binary to xmake's output directory so xmake run works
		local target_subdir = is_mode("release") and "release" or "debug"
		local src_binary = "target/" .. target_subdir .. "/kenzo"
		local dst_dir = target:targetdir()
		os.mkdir(dst_dir)
		os.cp(src_binary, path.join(dst_dir, "kenzo"))

		os.cd(oldir)
	end)
	on_clean(function(target)
		local oldir = os.cd("$(projectdir)/tools/kenzo")
		os.execv("cargo", { "clean" })
		os.cd(oldir)
	end)
end)

target("log_server", function()
	set_kind("binary")
	set_default(false)
	-- set_languages("rust")
	add_files("log_server/src/main.rs")
	add_packages(
		"cargo::prost",
		"cargo::tokio",
		"cargo::clap",
		"cargo::csv",
		"cargo::serde",
		"cargo::chrono"
	)
end)

target("toast.tools", function()
	set_kind("phony")
	set_default(false)
	add_deps("kenzo", "log_server")
end)
