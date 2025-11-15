cmake_minimum_required(VERSION 3.21...4.0)

set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake")

project(tigr VERSION 3.2.2 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_STANDARD 99)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(GLAD libglad REQUIRED IMPORTED_TARGET)

add_library(tigr STATIC)
option(USE_DEVELOPMENT_SOURCES "Use the loose sources files (for development)" ${PROJECT_IS_TOP_LEVEL})
if(USE_DEVELOPMENT_SOURCES)
	target_sources(tigr PRIVATE tigr.h src/tigr_amalgamated.c)
else()
	target_sources(tigr PRIVATE tigr.h tigr.c)
endif()
target_include_directories(tigr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tigr OpenGL::EGL PkgConfig::GLAD)

option(BUILD_EXAMPLE "Build the example" ${PROJECT_IS_TOP_LEVEL})
if(BUILD_EXAMPLE)
	add_executable(demo examples/switch-demo/switch-demo.c)
	target_link_libraries(demo tigr)
	set_target_properties(demo PROPERTIES LINKER_LANGUAGE CXX)

	nx_generate_nacp(demo.nacp NAME "TIGR switch demo"
		AUTHOR "TIGR authors, carstene1ns"
		VERSION ${PROJECT_VERSION})
	nx_create_nro(demo
		NACP demo.nacp
		ICON examples/switch-demo/tigr.jpg
		ROMFS examples/switch-demo/assets)
endif()

find_program(PERL_EXECUTABLE perl)
if(PERL_EXECUTABLE)
	add_custom_target(generate_font
		COMMAND ${PERL_EXECUTABLE} incbin.pl tigr_font.h font.png tigr_font
		BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/src/tigr_font.h
		DEPENDS src/font.png src/incbin.pl
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
		COMMENT "Regenerating font header..."
		VERBATIM)

	add_custom_target(generate_library
		COMMAND ${PERL_EXECUTABLE} bundle.pl ${CMAKE_CURRENT_SOURCE_DIR}/tigr.c tigr_amalgamated.c
		BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/tigr.c
		DEPENDS src/tigr_font.h src/bundle.pl
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
		COMMENT "Regenerating amalgamated library..."
		VERBATIM)
endif()
