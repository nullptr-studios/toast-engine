set_project("toast_engine")
set_languages("c++23")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", kind = "quiet"})

if is_mode("debug") then
	add_defines("_DEBUG")
elseif is_mode("release") then
	add_defines("NDEBUG", "GLM_FORCE_INTRINSICS")
end

-- Packages
add_requires(
	"glfw 3.4", "glm", "nlohmann_json", "spdlog", "lz4", "tracy", "glad", "stb",
	"yaml-cpp", "sol2", "tinyobjloader v2.0.0rc13", "imgui v1.92.5-docking",
	"imguizmo 1.91.3+wip", "spine-runtimes 4.2"
)

add_requireconfs("glfw", {configs = {shared = false}})
add_requireconfs("glm", "nlohmann_json", "spdlog", {configs = {header_only = true}})
add_requireconfs("spdlog", {configs = {fmt_external = false}})
add_requireconfs("imgui", {configs = {glfw = true, opengl3 = true}})

target("toast.engine", function()
	set_kind("static")
	add_files("src/**.cpp")
	add_headerfiles("inc/**.hpp", "inc/**.h")
	add_includedirs("inc", {public = true})
	add_includedirs("src")
	set_pcxxheader("src/pch.h")
	add_defines("TOAST_EDITOR") -- Deprecated

	add_packages(
		"glfw", "glm", "nlohmann_json", "spdlog", "lz4", "tracy", "glad", "stb",
		"yaml-cpp", "sol2", "tinyobjloader", "imgui", "imguizmo", "spine-runtimes",
		{public = true}
	)

	if is_plat("windows") then
		add_syslinks("comdlg32", "opengl32")
	elseif is_plat("linux") then
		add_syslinks("X11", "pthread", "dl", "GL")
	end
end)
