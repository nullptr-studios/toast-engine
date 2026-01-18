#!/usr/bin/env lua
-- clang-tools.lua
-- luarocks install luafilesystem
-- @author Dante Harper

local lfs = require("lfs")

local args = {}
for _, v in ipairs(arg) do
	args[v] = true
end

local do_format = args["-f"] or next(args) == nil
local do_tidy = args["-t"] or next(args) == nil
local do_fix = args["-i"] or false

-- Function to run a command
local function run_cmd(cmd)
	print("> " .. cmd)
	local ok, exit_type, code = os.execute(cmd)
	if not ok or (exit_type ~= "exit" or code ~= 0) then
		print(string.format("Command failed (type: %s, code: %s)", tostring(exit_type), tostring(code)))
		os.exit(1)
	end
end

local function is_source_file(filename)
	return filename:match("%.c$")
		or filename:match("%.cpp$")
		or filename:match("%.h$")
		or filename:match("%.hpp$")
		or filename:match("%.inl$")
end

local function GetFiles(path)
	local files = ""
	for file in lfs.dir(path) do
		if file == "." or file == ".." then goto continue end
		local fullpath = path .. "/" .. file
		local attrib = lfs.attributes(fullpath)
		if attrib.mode == "directory" then
			files = files .. " " .. GetFiles(fullpath)
		else
			if is_source_file(fullpath) then
				files = files .. " " .. fullpath
			end
		end
		::continue::
	end
	return files
end

local files = " " .. GetFiles("./src") .. " " .. GetFiles("./inc")
if do_format and do_fix then
	run_cmd("clang-format -i --verbose" .. files)
elseif do_format then
	run_cmd("clang-format -n --verbose" .. files)
end

if do_tidy and do_fix then
	run_cmd("clang-tidy --fix --fix-errors" .. files)
elseif do_tidy then
	run_cmd("clang-tidy" .. files)
end
