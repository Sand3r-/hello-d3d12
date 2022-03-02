cmake_minimum_required (VERSION 3.21)
project (hello-d3d12 C)

set(TARGET hello-d3d12)
add_executable(${TARGET})

set_target_properties(${TARGET} PROPERTIES C_STANDARD 17)
set_target_properties(${TARGET} PROPERTIES CMAKE_C_STANDARD_REQUIRED True)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")

set(CGLM_USE_TEST OFF CACHE INTERNAL "")

add_subdirectory(external/glfw)
add_subdirectory(external/cglm)
add_subdirectory(src)
set_directory_properties(PROPERTIES VS_STARTUP_PROJECT ${TARGET})