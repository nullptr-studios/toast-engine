local tools_dir = os.scriptdir()

target("kenzo")
    set_kind("phony")
    before_build(function (target)
        local mode = is_mode("release") and "release" or "debug"
        local manifest = path.join(tools_dir, "kenzo", "Cargo.toml")
        local args = {"build", "--manifest-path", manifest}
        if mode == "release" then table.insert(args, "--release") end
        os.execv("cargo", args)

        local bin_name = is_plat("windows") and "kenzo.exe" or "kenzo"
        local bin = path.join(tools_dir, "kenzo", "target", mode, bin_name)
        local out_dir = path.join(os.projectdir(), "build", os.host(), os.arch(), mode)
        os.mkdir(out_dir)
        os.cp(bin, out_dir)

        if not is_plat("windows") then
            os.execv("chmod", {"+x", path.join(out_dir, bin_name)})
        end
    end)
target_end()

target("toast.tools")
    set_kind("phony")
    add_deps("kenzo")
target_end()
