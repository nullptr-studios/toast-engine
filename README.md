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
- C# .NET8 toolchain
- VCPKG with `VCPKG_ROOT` set up in PATH

### Generation

Powershell
```pwsh
cmake --template win-relwithdebinfo
```

Linux
```bash
cmake --template linux-relwithdebinfo
```

Or using the Nix flake
```bash
nix develop
cmake-gen
```

### Build

```bash
cmake --build --template win-relwithdebinfo --target editor
```

Or using the Nix flake
```bash
cmake-build
```

### Tests

```bash
ctest --test-dir build/Debug -C Debug
```
