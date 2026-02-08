-- This target contains the core Toast Engine
target("toast.engine", function()
	set_kind("shared")
	add_includedirs("include/toast")
	add_includedirs("include", "ffi", { public = true })

	add_files("src/**.cpp")
	add_headerfiles("ffi/**.h", "include/toast/**.hpp", { prefixdir = "toast" })
end)

-- Rust modules
includes("rust/xmake.lua")
