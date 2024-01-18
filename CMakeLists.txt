cmake_minimum_required (VERSION 3.14)
project(utils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(fmt REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost COMPONENTS system REQUIRED)

add_library(asio_utils SHARED
  utils/src/async_io_context.cpp
  utils/src/can.cpp
  utils/src/logger.cpp
  utils/src/mqtt_client.cpp
  utils/src/string_util.cpp
  utils/src/timer.cpp
  utils/src/udp_client.cpp
)

target_include_directories(asio_utils
  PUBLIC    utils/include/
)


target_link_libraries(asio_utils
  PRIVATE   Boost::boost
  PRIVATE   mosquitto
  PRIVATE   Threads::Threads
  PRIVATE   fmt::fmt
)

set(UTIL_HEADERS
  utils/include/async_io_context.hpp
  utils/include/can/can.hpp
  utils/include/logger.hpp
  utils/include/mqtt_client.hpp
  utils/include/string_util.hpp
  utils/include/timer.hpp
  utils/include/udp_client.hpp
)

set_target_properties(asio_utils
  PROPERTIES
  PUBLIC_HEADER  "${UTIL_HEADERS}"
)


install (
  TARGETS asio_utils
  LIBRARY DESTINATION lib
  PUBLIC_HEADER DESTINATION include/asio_utils
)
