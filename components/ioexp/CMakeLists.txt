set(SOURCES
	MCP23017.cpp
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES driver
)