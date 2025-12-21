# Recomended Tools

This is a list of the tools recomended to work on Toast Engine

## Dependencies

For working on the project, the following tools need to be installed on your computer:
- Visual Studio Toolchain for MSVC compiler on windows / GNU C++ compiler on Linux
- LLVM C++ tools for clang-format and clang-tidy (you can install it by running `winget install LLVM.LLVM`)
- CMake (version 3.25 or up, version 3.30 or up recommended)
- Ninja
- Lua 5.4 (you can install it by running `winget install Devcom.Lua`)
- Vulkan SDK for the SPIR-V shader compiler (you can install glslc if you dont want to have to install the full SDK)

## IDEs

For C++ code on the engine, an IDE with CMake support is required to work on the project.
JetBrains CLion is recommended over Visual Studio as while VS supports CMakeLists, it's not
as good as an experience to use. Also, the support for the GLSL programming is better on CLion.

## Compiling

Compiling using the IDEs tools is fine most of the time. However, a script to compile will be provided
that will add some additional steps like downloading submodules, compiling shaders, packing files, etc.

You can use our compile script by running `lua .\compile.lua`. The command also has the flags
`-g` to just run the build system generation, `-b` to just build with a generated system and `-R` to
make the script build on Release. The script will use Visual Studio 17 as the generator, MSVC as the
compiler for Windows and Ninja as the generator for the compile_commands file used by LLVM on Windows.
The script will use Unix Makefiles as the generator and G++ as the compiler on Linux.

## CI

The whole CI project for the team can be seen on the .gitlab-ci.yml file. However, a script exist to
check if the CI will pass before pushing. You can run the script with `lua .\clang-tools.lua`

By default this script will run the unit tests but you can check more things with the following flags:
- `-f` will run clang-format
- `-t` will run clang-tidy
- `-c` will run ctest
- `-i` will tell clang-format and clang-tidy to modify your files and try to fix them automatically.
    This is not a bad idea; however, clang-tidy will not be able to fix all the errors and you will
    have to manually fix some of them
- `-R` will run ctest on release (project needs to be built on release to work)
