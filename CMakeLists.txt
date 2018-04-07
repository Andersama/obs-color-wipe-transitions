project(obs-color-wipe-transitions)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(LibObs REQUIRED)

include_directories(
	SYSTEM "${CMAKE_SOURCE_DIR}/libobs"
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
)

set(obs-color-wipe-transitions_HEADERS
	)

set(obs-color-wipe-transitions_SOURCES
	transition-animated-wipe.c
)

add_library(obs-color-wipe-transitions MODULE
	${obs-color-wipe-transitions_SOURCES}
	${obs-color-wipe-transitions_HEADERS}
)

target_link_libraries(obs-color-wipe-transitions
	libobs
)

install_obs_plugin_with_data(obs-color-wipe-transitions data)
