# Toast Engine

Toast Engine is an open source game engine done by Nullptr* Studios

Credits:
- Xein
- Dario
- Dante
- Iñaki
- Alexey
- Akaansh

## Building

Dependencies:
- cmake 3.25+
- ninja
- vcpkg (manifest mode)
- cargo (optional, for Rust tools)
- dotnet 10 (optional, for player)

Configure + build with vcpkg toolchain (CI bootstraps vcpkg automatically):
```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

Run tests:
```
ctest --test-dir build
```

To use this engine in a project, include it as a submodule and go to Get Started on the documentation
