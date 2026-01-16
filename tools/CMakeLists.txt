
add_executable(toast.packer ToastPacker/Packer.cpp)
target_link_libraries(toast.packer PRIVATE lz4)

add_executable(toast.unpacker ToastPacker/Unpacker.cpp)
target_link_libraries(toast.unpacker PRIVATE lz4)
