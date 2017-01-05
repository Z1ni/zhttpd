cmake_minimum_required(VERSION 2.8)

project(zhttpd)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -Wall -gstabs")

add_definitions(-D_GNU_SOURCE)	# Only for testing, enables the use of asprintf

include_directories(
	include
)

add_executable(${CMAKE_PROJECT_NAME}
	src/main.c
	src/child.c
	src/utils.c
)

target_link_libraries(${CMAKE_PROJECT_NAME}
)
