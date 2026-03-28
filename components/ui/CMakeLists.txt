
set (REQ
	esp_timer
	iocfg
	ioexp
	oled
	esp_event
	button
	output
	qrcodegen
	nvs_flash
	app_update
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
	EvtTimeScreen.cpp
	MoistThrScreen.cpp
	TimezoneScreen.cpp
	WifiProvScreen.cpp
	WifiStatusScreen.cpp
	HapQrScreen.cpp
	HapResetScreen.cpp
	InfoScreen.cpp
	MoistCalScreen.cpp
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES ${REQ}
)