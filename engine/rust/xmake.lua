local rust_dir = os.scriptdir()

-- Builds the Cargo workspace and links the output into the target
rule("toast.cargo")
    on_load(function(target)
        local mode = is_mode("release") and "release" or "debug"
        target:add("linkdirs", path.join(rust_dir, "target", mode))
        target:add("links", "hello")
        -- Rust stdlib on Windows requires these system libraries
        if is_plat("windows") then
            target:add("syslinks", "ws2_32", "userenv", "bcrypt", "ntdll")
        end
    end)

    before_build(function(target)
        local args = {"build", "--manifest-path", path.join(rust_dir, "Cargo.toml")}
        if is_mode("release") then
            table.insert(args, "--release")
        end
        os.execv("cargo", args)
    end)
rule_end()

target("toast.engine")
    add_rules("toast.cargo")
target_end()
