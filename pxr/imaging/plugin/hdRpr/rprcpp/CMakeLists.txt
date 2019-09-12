target_sources(hdRpr PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/rprError.h
	${CMAKE_CURRENT_SOURCE_DIR}/rprObject.h
	${CMAKE_CURRENT_SOURCE_DIR}/rprContext.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rprContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rprFramebuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rprFramebuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rprFramebufferGL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rprFramebufferGL.cpp)
