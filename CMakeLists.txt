cmake_minimum_required(VERSION 3.13.2)

set(PROJ_NAME hammernail)
project(${PROJ_NAME} CXX C)
set(EXE_NAME ${PROJ_NAME})
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

include_directories(
	src
	)

add_library(amd64 STATIC
	src/amd64/amd64.c
	src/amd64/fpu.c
	src/amd64/i386.c
	)

target_include_directories(amd64 PUBLIC src/amd64)

add_executable(${PROJ_NAME}
	src/main.cpp
	src/mini-rv64.cpp
)

add_subdirectory(vendor/raylib)
target_link_libraries(${PROJ_NAME} raylib amd64)

set_target_properties(${PROJ_NAME} PROPERTIES
	LINKER_LANGUAGE CXX
)
	
target_compile_options(${EXE_NAME} PUBLIC 
	-g -Wall 
	$<$<CONFIG:Debug>:-g> 
	$<$<CONFIG:Release>: -O2>
)
target_link_options(${EXE_NAME} PUBLIC 
	$<$<CONFIG:RELEASE>: -s>
)
