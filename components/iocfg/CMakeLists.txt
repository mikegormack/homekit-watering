
set (REQ
)

set(incl
	"."
	${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES ${REQ}
)