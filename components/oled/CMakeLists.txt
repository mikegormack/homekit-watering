set(SOURCES
	OLEDDisplay.cpp
	SSD1306I2C.cpp
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES driver
)