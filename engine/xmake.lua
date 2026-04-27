rule("sync-headers")
before_build(function(target)
	cprint("${green}[100%%]: ${magenta}<%s> ${reset}sync-headers", target:name())
	import("core.base.option")

	local base_dir = os.scriptdir()
	local src_dir  = path.join(base_dir, "src")
	local dst_dir  = path.join(base_dir, "include")

	if os.isdir(src_dir) then
		if os.exists(dst_dir) and not os.isdir(dst_dir) then
			os.rm(dst_dir)
		end
		os.mkdir(dst_dir)

		local count = 0

		for _, filepath in ipairs(files) do
			local content = io.readfile(filepath)
			if content and content:find("TOAST_API", 1, true) then
				local rel_path = path.relative(filepath, src_dir)
				os.cp(filepath, dst_dir, { rootdir = src_dir })
				count = count + 1
			end
		end

		-- Optional: A final summary if any files were moved
		if count > 0 then
			vprint("Total files synced: %d", count)
		end
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
