set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Static libavcodec has x86 asm that can't be linked into libtoast_engine.so (R_X86_64_PC32 against ff_h264_cabac_tables)
if (PORT STREQUAL "tracy" OR PORT STREQUAL "ffmpeg")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif ()
