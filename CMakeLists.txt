cmake_minimum_required(VERSION 3.5.0)
project(ffrunner VERSION 0.1.0 LANGUAGES C)

add_executable(ffrunner ffrunner.c graphics.c requests.c logging.c ffrunner.rc)

if (MSVC)
    set_target_properties(ffrunner PROPERTIES LINK_FLAGS "/LARGEADDRESSAWARE")
endif()

set_target_properties(ffrunner PROPERTIES C_STANDARD 99)

target_link_libraries(ffrunner wininet)

