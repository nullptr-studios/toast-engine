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

        -- Grouping logic
        set_group("tests/" .. dir)
        add_tests("default", { group = dir })
        
				add_rpathdirs("./", "../")
    end)
end
