# FMOD
if(WIN32)
    set(FMOD_PLATFORM "windows")
    set(FMOD_ARCH "x64")
elseif(UNIX AND NOT APPLE)
    set(FMOD_PLATFORM "linux")
    set(FMOD_ARCH "x86_64")
endif()

set(FMOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(FMOD_LIB_DIR "${FMOD_DIR}/lib/${FMOD_PLATFORM}")
set(FMOD_INCLUDE_DIR "${FMOD_DIR}/include")

# Determine DLL paths based on build type and compiler
if(WIN32)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # For GCC/Clang on Windows
        set(FMOD_CORE_DLL_DEBUG "${FMOD_LIB_DIR}/fmodL.dll")
        set(FMOD_CORE_DLL_RELEASE "${FMOD_LIB_DIR}/fmod.dll")
        set(FMOD_STUDIO_DLL_DEBUG "${FMOD_LIB_DIR}/fmodstudioL.dll")
        set(FMOD_STUDIO_DLL_RELEASE "${FMOD_LIB_DIR}/fmodstudio.dll")
        set(USE_MSVC_IMPLIB FALSE)
    else()
        # For MSVC
        set(FMOD_CORE_DLL_DEBUG "${FMOD_LIB_DIR}/fmodL.dll")
        set(FMOD_CORE_IMPLIB_DEBUG "${FMOD_LIB_DIR}/fmodL_vc.lib")
        set(FMOD_CORE_DLL_RELEASE "${FMOD_LIB_DIR}/fmod.dll")
        set(FMOD_CORE_IMPLIB_RELEASE "${FMOD_LIB_DIR}/fmod_vc.lib")
        set(FMOD_STUDIO_DLL_DEBUG "${FMOD_LIB_DIR}/fmodstudioL.dll")
        set(FMOD_STUDIO_IMPLIB_DEBUG "${FMOD_LIB_DIR}/fmodstudioL_vc.lib")
        set(FMOD_STUDIO_DLL_RELEASE "${FMOD_LIB_DIR}/fmodstudio.dll")
        set(FMOD_STUDIO_IMPLIB_RELEASE "${FMOD_LIB_DIR}/fmodstudio_vc.lib")
        set(USE_MSVC_IMPLIB TRUE)
    endif()
elseif(UNIX AND NOT APPLE)
    set(FMOD_CORE_LIB "${FMOD_LIB_DIR}/libfmod.so")
    set(FMOD_STUDIO_LIB "${FMOD_LIB_DIR}/libfmodstudio.so")
endif()

# FMOD Core Library
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_library(fmod INTERFACE IMPORTED GLOBAL)
    target_include_directories(fmod INTERFACE ${FMOD_INCLUDE_DIR})
    
    # Add both DLLs as link options to handle both debug and release
    target_link_options(fmod INTERFACE 
        "$<$<CONFIG:Debug>:${FMOD_CORE_DLL_DEBUG}>"
        "$<$<CONFIG:Release>:${FMOD_CORE_DLL_RELEASE}>"
        "$<$<CONFIG:RelWithDebInfo>:${FMOD_CORE_DLL_RELEASE}>"
        "$<$<CONFIG:MinSizeRel>:${FMOD_CORE_DLL_RELEASE}>"
    )
else()
    add_library(fmod SHARED IMPORTED GLOBAL)
    target_include_directories(fmod INTERFACE ${FMOD_INCLUDE_DIR})
    
    if(WIN32)
        # MSVC path
        set_target_properties(fmod PROPERTIES
            IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
            IMPORTED_LOCATION_DEBUG "${FMOD_CORE_DLL_DEBUG}"
            IMPORTED_IMPLIB_DEBUG "${FMOD_CORE_IMPLIB_DEBUG}"
            IMPORTED_LOCATION_RELEASE "${FMOD_CORE_DLL_RELEASE}"
            IMPORTED_IMPLIB_RELEASE "${FMOD_CORE_IMPLIB_RELEASE}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${FMOD_CORE_DLL_RELEASE}"
            IMPORTED_IMPLIB_RELWITHDEBINFO "${FMOD_CORE_IMPLIB_RELEASE}"
            IMPORTED_LOCATION_MINSIZEREL "${FMOD_CORE_DLL_RELEASE}"
            IMPORTED_IMPLIB_MINSIZEREL "${FMOD_CORE_IMPLIB_RELEASE}"
        )
    else()
        # Linux path
        set_target_properties(fmod PROPERTIES
            IMPORTED_LOCATION "${FMOD_CORE_LIB}"
        )
    endif()
endif()

# FMOD Studio Library
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_library(fmod_studio INTERFACE IMPORTED GLOBAL)
    target_include_directories(fmod_studio INTERFACE ${FMOD_INCLUDE_DIR})
    
    # Add both DLLs as link options to handle both debug and release
    target_link_options(fmod_studio INTERFACE 
        "$<$<CONFIG:Debug>:${FMOD_STUDIO_DLL_DEBUG}>"
        "$<$<CONFIG:Release>:${FMOD_STUDIO_DLL_RELEASE}>"
        "$<$<CONFIG:RelWithDebInfo>:${FMOD_STUDIO_DLL_RELEASE}>"
        "$<$<CONFIG:MinSizeRel>:${FMOD_STUDIO_DLL_RELEASE}>"
    )
    target_link_libraries(fmod_studio INTERFACE fmod)
else()
    add_library(fmod_studio SHARED IMPORTED GLOBAL)
    target_include_directories(fmod_studio INTERFACE ${FMOD_INCLUDE_DIR})
    target_link_libraries(fmod_studio INTERFACE fmod)
    
    if(WIN32)
        # MSVC path
        set_target_properties(fmod_studio PROPERTIES
            IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
            IMPORTED_LOCATION_DEBUG "${FMOD_STUDIO_DLL_DEBUG}"
            IMPORTED_IMPLIB_DEBUG "${FMOD_STUDIO_IMPLIB_DEBUG}"
            IMPORTED_LOCATION_RELEASE "${FMOD_STUDIO_DLL_RELEASE}"
            IMPORTED_IMPLIB_RELEASE "${FMOD_STUDIO_IMPLIB_RELEASE}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${FMOD_STUDIO_DLL_RELEASE}"
            IMPORTED_IMPLIB_RELWITHDEBINFO "${FMOD_STUDIO_IMPLIB_RELEASE}"
            IMPORTED_LOCATION_MINSIZEREL "${FMOD_STUDIO_DLL_RELEASE}"
            IMPORTED_IMPLIB_MINSIZEREL "${FMOD_STUDIO_IMPLIB_RELEASE}"
        )
    else()
        # Linux path
        set_target_properties(fmod_studio PROPERTIES
            IMPORTED_LOCATION "${FMOD_STUDIO_LIB}"
        )
    endif()
endif()
