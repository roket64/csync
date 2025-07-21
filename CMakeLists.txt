cmake_minimum_required(VERSION 3.10.0)
project(csync
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS CMAKE_BUILD_TYPE)
message(STATUS "Build Type: " ${CMAKE_BUILD_TYPE})

set(CSYNC_CXX_FLAGS_DEBUG
  "-g -O0 -Wall -Wextra -Wpedantic -Werror"
)

set(CSYNC_CXX_FLAGS_RELEASE
  "-O3 -DNDEBUG -ffunction-sections -fdata-sections"
)

set(CMAKE_CXX_FLAGS_DEBUG
  ${CSYNC_CXX_FLAGS_DEBUG}
)

set(CMAKE_CXX_FLAGS_RELEASE
  ${CSYNC_CXX_FLAGS_RELEASE}
)

add_subdirectory(tests)

add_executable(csync
  src/main.cpp
)
