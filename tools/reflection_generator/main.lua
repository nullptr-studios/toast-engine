---
--- File: main.lua
--- Author: Xein
--- Date: 19 May 2026
---

local treesitter = require("ltreesitter")
local cxx_language = treesitter.load("./cpp.dll", "cpp")
local cxx_parser = cxx_language:parser()

local source_code = [[
#include <stdio.h>

// a function that does stuff
static void stuff_doer(void) {
    printf("I'm doing stuff! :D\n");
}

int main(int argc, char **argv) {
    stuff_doer();
    return 0;
}
]]

local tree = cxx_parser:parse_string(source_code)

local function print_tree_node(node, prefix, is_last)
    if not node then return end

    local node_text = node:source()

    node_text = node_text:gsub("\r?\n", " ")
    if #node_text > 30 then
        node_text = node_text:sub(1, 27) .. "..."
    end

    local node_info = string.format("%s (\"%s\")", node:type(), node_text)

    print(prefix .. (is_last and "+-- " or "|-- ") .. node_info)

    local next_prefix = prefix .. (is_last and "    " or "|   ")

    local children = {}
    for child in node:named_children() do
        table.insert(children, child)
    end

    for i, child in ipairs(children) do
        local is_child_last = (i == #children)
        print_tree_node(child, next_prefix, is_child_last)
    end
end

local root = tree:root()
print(root:type())

local root_children = {}
for child in root:named_children() do
    table.insert(root_children, child)
end

for i, child in ipairs(root_children) do
    print_tree_node(child, "", i == #root_children)
end
