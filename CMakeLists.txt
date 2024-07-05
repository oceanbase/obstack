cmake_minimum_required(VERSION 3.12.2)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

project(obstack
  VERSION 0.0.1
  DESCRIPTION "CallStack Collector && Parser"
  LANGUAGES CXX C ASM)

message(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})

set(DEP_DIR "${CMAKE_SOURCE_DIR}/deps")
set(DEVEL_DIR "${CMAKE_SOURCE_DIR}/deps/usr/local/oceanbase/deps/devel")
set(DEVTOOLS_DIR "${CMAKE_SOURCE_DIR}/deps/usr/local/oceanbase/devtools")

find_program(CC gcc
  PATHS ${DEVTOOLS_DIR}/bin
  NO_DEFAULT_PATH)
find_program(CXX g++
  PATHS ${DEVTOOLS_DIR}/bin
  NO_DEFAULT_PATH)
if (CC AND CXX)
  set(CMAKE_C_COMPILER ${CC})
  set(CMAKE_CXX_COMPILER ${CXX})
else()
  message(FATAL_ERROR "can't find suitable compiler")
endif()
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(src)
