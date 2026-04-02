
set (req
	ioexp
	iocfg
	esp_event
	esp_hap_core
	esp_hap_apple_profiles
	esp_hap_extras
	esp_wifi
	lwip
	wifi_provisioning
	#app_wifi
	app_hap_setup_payload
	button
	ui
	output
	input
	qrcodegen
	esp_http_server
	app_update
	json
)

set(incl
	"."
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

set (srcs
	app_main.cpp
	app_wifi_handler.cpp
	ValveChannel.cpp
	Scheduler.cpp
	OtaServer.cpp
	ControlServer.cpp
	WebBase.cpp
	EventLog.cpp
)

idf_component_register(
	SRCS ${srcs}
	REQUIRES ${req}
	INCLUDE_DIRS ${incl}
)
