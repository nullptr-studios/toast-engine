local function run_cmd(cmd)
	print("> " .. cmd)
	local ok, exit_type, code = os.execute(cmd)
	if not ok or (exit_type ~= "exit" or code ~= 0) then
		print(string.format("Command failed (type: %s, code: %s)", tostring(exit_type), tostring(code)))
		os.exit(1)
	end
end

local os_env = os.getenv("OS")
if os_env and os_env:find("Windows") then
    run_cmd("cmake -B build -G \"Visual Studio 18 2026\" -DCMAKE_BUILD_TYPE=Debug")
else
    run_cmd("cmake -B build -G \"Unix Makefiles\" -DCMAKE_BUILD_TYPE=Debug")
end

run_cmd("cmake --build build -j 16") -- using 16 threads