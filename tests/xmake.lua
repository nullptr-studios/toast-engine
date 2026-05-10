-- Run Tests:       xmake test
-- Run Test Group:  xmake test -g events

local scriptdir = os.scriptdir()
for _, filepath in ipairs(os.files(path.join(scriptdir, "**.cpp"))) do
	local relpath = path.relative(filepath, scriptdir)
	local dir = path.directory(relpath)
	local target_name = relpath:gsub("%.cpp$", "")

	target(target_name, function()
		set_kind("binary")
		set_default(false)

		add_files(filepath)
		add_deps("toast.engine")
		if not is_plat("windows") then
			add_syslinks("stdc++exp")
		end
		if is_plat("windows") and (get_config("toolchain") == "mingw" or get_config("toolchain") == "gcc") then
			add_syslinks("stdc++exp")
		end

		add_defines("UNIT_TESTING")


		-- Grouping logic
		set_group("tests/" .. dir)
		add_tests("default", { group = dir })

		add_rpathdirs("./", "../")
	end)
end
