add_requires("asio 1.36.0") -- networking

target("toast.engine", function()
	set_kind("shared")
	add_includedirs("include", { public = true })
	add_includedirs(".", "src", "external", { public = false })
	add_defines("TOAST_EXPORT")
	add_rpathdirs("$ORIGIN")

	add_files("**.cpp")
	add_headerfiles("ffi/**.h", "include/**.hpp", { prefixdir = "toast" })

	-- External libraries go here -x
	add_packages("asio")

	if is_plat("linux") then
		add_syslinks("pthread")
		add_shflags("-Wl,--no-as-needed") 
	end
end)

