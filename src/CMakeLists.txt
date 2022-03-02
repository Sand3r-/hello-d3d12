target_sources(${TARGET} PRIVATE
	main.c
)

list(APPEND LIBRARIES d3d12.lib)
list(APPEND LIBRARIES dxgi.lib)
list(APPEND LIBRARIES dxguid.lib)
list(APPEND LIBRARIES d3dcompiler.lib)
list(APPEND LIBRARIES glfw)
list(APPEND LIBRARIES cglm)

target_link_libraries(${TARGET} ${LIBRARIES})