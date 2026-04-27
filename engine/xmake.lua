rule("sync-headers")
before_build(function(target)
	import("core.base.option")

	local base_dir = os.scriptdir()
	local src_dir  = path.join(base_dir, "src")
	local dst_dir  = path.join(base_dir, "include")

	if not os.isdir(src_dir) then
		return
	end

	-- Ensure include directory exists and is clean
	os.mkdir(dst_dir)

	local count        = 0

	-- Use os.files to find all headers in the src directory recursively
	-- This is more reliable than relying on target:sourcefiles()
	local header_files = os.files(path.join(src_dir, "**.h"))
	local hpp_files    = os.files(path.join(src_dir, "**.hpp"))

	-- Combine lists
	local all_files    = table.join(header_files, hpp_files)

	for _, filepath in ipairs(all_files) do
		local content = io.readfile(filepath)

		if content and content:find("TOAST_API", 1, true) then
			-- 1. Copy the main header
			os.cp(filepath, dst_dir, { rootdir = src_dir })
			count = count + 1

			-- 2. Scan for and copy .inl dependencies
			for inl_include in content:gmatch('#include%s+"([^"]+%.inl)"') do
				-- Find the .inl file relative to the current header
				local current_dir = path.directory(filepath)
				local inl_path = path.join(current_dir, inl_include)

				if os.isfile(inl_path) then
					os.cp(inl_path, dst_dir, { rootdir = src_dir })
					vprint("Synced dependency: %s", inl_include)
				end
			end
		end
	end

	if count > 0 then
		cprint("${green}[sync-headers]: ${reset}Synced %d API headers (and their .inl files) to /include", count)
	end
end)
rule_end()

add_requires("asio 1.36.0") -- networking

target("toast.engine", function()
	set_kind("shared")
	add_includedirs(".", "src", "external", { public = false })
	add_includedirs("include", { public = true })
	add_defines("TOAST_EXPORT")
	add_rpathdirs("$ORIGIN")

	add_files("**.cpp")
	add_headerfiles("src/(**.hpp)", { public = false, extra = { check = true } })
	add_headerfiles("ffi/(**.h)", "include/toast/(**.hpp)", { prefixdir = "toast", extra = { check = true } })

	-- External libraries go here -x
	add_packages("asio")
	add_ldflags("-lstdc++exp") -- (adds library for stacktrace) i needed this for std::stacktrace

	-- Apply clang-format rule
	add_rules("clang-format")
	add_rules("sync-headers")

	if is_plat("linux") then
		add_syslinks("pthread")
		add_shflags("-Wl,--no-as-needed")
	end
end)
