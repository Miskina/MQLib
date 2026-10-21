cmake_minimum_required(VERSION 3.22)

project(mqlib_metrics VERSION 0.1.0 LANGUAGES CXX)

set(MQLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "MQLib source tree")

# Same language mode and optimisation as MQLib's Makefile (-std=c++0x -O2, no NDEBUG),
# so the floating-point results match bin/MQLib.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

execute_process(COMMAND git -C "${MQLIB_ROOT}" rev-parse HEAD
                OUTPUT_VARIABLE MQLIB_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
execute_process(COMMAND git -C "${MQLIB_ROOT}" status --porcelain -- src include
                OUTPUT_VARIABLE MQLIB_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(NOT MQLIB_COMMIT)
  set(MQLIB_COMMIT "unknown")
endif()
if(MQLIB_DIRTY)
  set(MQLIB_COMMIT "${MQLIB_COMMIT}-dirty")
endif()

find_package(Python 3.12 REQUIRED COMPONENTS Interpreter Development.Module)
Python_add_library(_core MODULE WITH_SOABI
  src/bindings.cpp
  "${MQLIB_ROOT}/src/problem/instance.cpp"
  "${MQLIB_ROOT}/src/problem/max_cut_instance.cpp"
  "${MQLIB_ROOT}/src/problem/qubo_instance.cpp"
  "${MQLIB_ROOT}/src/metrics/max_cut_metrics.cpp"
  "${MQLIB_ROOT}/src/util/random.cpp")
target_include_directories(_core PRIVATE "${MQLIB_ROOT}/include")
target_compile_definitions(_core PRIVATE
  "MQLIB_COMMIT=\"${MQLIB_COMMIT}\""
  "MQLIB_BUILD_FLAGS=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} ${CMAKE_BUILD_TYPE} ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE} c++11\"")
install(TARGETS _core LIBRARY DESTINATION mqlib_metrics)
