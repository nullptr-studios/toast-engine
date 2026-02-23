add_library(imguizmo STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImCurveEdit.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGradient.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImSequencer.cpp
)
target_include_directories(imguizmo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imguizmo PUBLIC imgui)