
set (req
	ioexp
	iocfg
	esp_event
	esp_hap_core
	esp_hap_apple_profiles
	esp_hap_extras
	app_wifi
	app_hap_setup_payload
	button
	ui
	output
)

set(incl
	"."
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

set (srcs
	app_main.cpp
)

idf_component_register(
	SRCS ${srcs}
	REQUIRES ${req}
	INCLUDE_DIRS ${incl}
)
