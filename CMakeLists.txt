cmake_minimum_required(VERSION 3.13.2)

set(PROJ_NAME hammernail)
project(${PROJ_NAME} CXX C)
set(EXE_NAME ${PROJ_NAME})
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_executable(${PROJ_NAME}
	src/main.cpp
	src/mini-rv64.cpp
)

add_subdirectory(vendor/raylib)
target_link_libraries(${PROJ_NAME} raylib)
target_include_directories(${PROJ_NAME} PUBLIC src)
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
