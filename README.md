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

Configure + build with presets (requires `VCPKG_ROOT` set):
```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Run tests:
```
ctest --preset windows-msvc-debug
```

To use this engine in a project, include it as a submodule and go to Get Started on the documentation
