
set (req
	ioexp
	oled
	esp_event
	esp_hap_core
	esp_hap_apple_profiles
	esp_hap_extras
	app_wifi
	app_hap_setup_payload
	button
)

set(incl
	"."
	#${HOMEKIT_PATH}/components
	#${HOMEKIT_PATH}/components/homekit
	#${HOMEKIT_PATH}/examples/common
	${CMAKE_CURRENT_SOURCE_DIR}
)

set (srcs
	app_main.cpp
	ui.cpp
	homeScreen.cpp
	setOnTimeScreen.cpp
)

idf_component_register(
	SRCS ${srcs}
	REQUIRES ${req}
	INCLUDE_DIRS ${incl}
)
