
set (REQ
	esp_timer
	iocfg
	ioexp
	oled
	esp_event
	button
	output
)

set(INCL
	"."
	${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES
	UI.cpp
	Screen.cpp
	HomeScreen.cpp
	SettingsMenu.cpp
	SetEvtTimeScreen.cpp
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES ${REQ}
)