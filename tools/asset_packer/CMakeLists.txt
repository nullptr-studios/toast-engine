project(asset_packer NONE)

set(CARGO_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CARGO_TARGET_DIR "${OUTPUT_ROOT}/$<CONFIG>/cargo")
set(CARGO_PROFILE_NAME "$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>,release,debug>")
set(CARGO_PROFILE_FLAG "$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>,release,dev>")

find_program(CARGO_EXECUTABLE cargo REQUIRED)

# One cargo build for all three [[bin]] entries of the crate
add_custom_target(packer ALL
        COMMAND ${CARGO_EXECUTABLE} build --profile "${CARGO_PROFILE_FLAG}" --target-dir "${CARGO_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -DCARGO_TARGET_DIR=${CARGO_TARGET_DIR}
            -DPROFILE_NAME=${CARGO_PROFILE_NAME}
            "-DBINARY_NAMES=packer;unpacker;pack_viewer"
            -DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -DDEST_DIR=${OUTPUT_ROOT}/$<CONFIG>/packer
            -P ${CMAKE_SOURCE_DIR}/cmake/install_cargo_output.cmake
        WORKING_DIRECTORY ${CARGO_PROJECT_DIR}
        COMMENT "[cargo] Building asset packer/unpacker/pack_viewer (${CARGO_PROFILE_NAME})..."
        VERBATIM
)

# Name only targets so add_dependencies(editor unpacker) still resolves
add_custom_target(unpacker ALL)
add_dependencies(unpacker packer)

add_custom_target(pack_viewer ALL)
add_dependencies(pack_viewer packer)
