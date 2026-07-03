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

### Prerequisites
- CMake >=3.24
- A C++23 compiler (VS18 and GCC15 tested)
- Rustc and Cargo
- C# .NET10 toolchain
- VCPKG with `VCPKG_ROOT` set up in PATH

### Generation

Powershell
```pwsh
cmake -B build/Debug -G "Visual Studio 18 2026" -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

Linux
```bash
cmake -B build/Debug -G "Visual Studio 18 2026" -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

Or using the Nix flake
```bash
nix develop
cmake-gen
```

### Build

```bash
cmake --build ./build/Debug
```

```bash
dotnet run --project editor
```

Or using the Nix flake
```bash
cmake-build
```

### Tests

```bash
ctest --test-dir build/Debug -C Debug
```
