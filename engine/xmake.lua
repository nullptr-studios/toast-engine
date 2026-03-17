add_requires("asio 1.36.0") -- networking

-- This target contains the core Toast Engine
target("toast.engine", function()
	set_kind("shared")
	add_includedirs("include/toast")
	add_includedirs("generated")
	add_includedirs("external")
	add_includedirs("include", "ffi", { public = true })
	add_defines("TOAST_EXPORT")
	add_rpathdirs("$ORIGIN")

	add_files("src/**.cpp")
	add_headerfiles("ffi/**.h", "include/toast/**.hpp", { prefixdir = "toast" })
	add_headerfiles("src/*.hpp", {private = true})

	add_packages("asio")

	if is_plat("linux") then
		add_syslinks("pthread")
		add_shflags("-Wl,--no-as-needed") 
	end
end)

-- Rust modules
includes("rust/xmake.lua")
