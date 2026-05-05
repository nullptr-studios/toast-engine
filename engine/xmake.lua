---@diagnostic disable: undefined-field, undefined-global, param-type-mismatch
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
	add_ldflags("-lstdc++exp", { tools = { "clang", "gcc" }, public = true }) -- adds library for stacktrace

	-- Apply clang-format rule
	-- FIX THIS SO CLANG FORMAT ALWAYS RUNS BEFORE THE DANTE HEADERS BULLSHIT
	--add_rules("clang-format")
	add_rules("sync-headers")

	if is_plat("linux") then
		add_syslinks("pthread")
		add_shflags("-Wl,--no-as-needed")
	end
end)





-- Include Folder Generation
rule("sync-headers")
---@diagnostic disable-next-line: unused-local
before_build(function(target)
	import("core.base.option")

	local base_dir = os.scriptdir()
	local src_dir  = path.join(base_dir, "src")
	local dst_dir  = path.join(base_dir, "include")

	-- Define the header you want to append
	local notice   = [[
// ============================================================
// AUTO-GENERATED FILE - DO NOT MODIFY DIRECTLY
// Changes will not persist
// ============================================================
]]

	if not os.isdir(src_dir) then return end
	if os.exists(dst_dir) then os.rm(dst_dir) end
	os.mkdir(dst_dir)

	local count = 0
	local all_files = table.join(os.files(path.join(src_dir, "**.h")), os.files(path.join(src_dir, "**.hpp")))

	for _, filepath in ipairs(all_files) do
		local content = io.readfile(filepath)

		if content and content:find("TOAST_API", 1, true) then
			-- Determine destination path while preserving subfolder structure
			local rel_path = path.relative(filepath, src_dir)
			local dst_path = path.join(dst_dir, rel_path)

			-- Ensure the subfolder exists in the destination
			os.mkdir(path.directory(dst_path))

			-- Write notice + content to the new file
			io.writefile(dst_path, notice .. "\n" .. content)
			count = count + 1

			-- Scan for and copy .inl dependencies
			for inl_include in content:gmatch('#include%s+"([^"]+%.inl)"') do
				local current_dir = path.directory(filepath)
				local inl_src_path = path.join(current_dir, inl_include)

				if os.isfile(inl_src_path) then
					local inl_rel_path = path.relative(inl_src_path, src_dir)
					local inl_dst_path = path.join(dst_dir, inl_rel_path)

					local inl_content = io.readfile(inl_src_path)
					os.mkdir(path.directory(inl_dst_path))
					io.writefile(inl_dst_path, notice .. "\n" .. inl_content)
				end
			end
		end
	end

	if count > 0 then
		cprint("${green}[sync-headers]: ${reset}Synced API")
	end
end)
rule_end()
