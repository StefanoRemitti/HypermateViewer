cmake_minimum_required(VERSION 3.16)
project(device-discovery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "device-discovery targets Linux (netlink/AF_PACKET APIs are required)")
endif()

find_package(Threads REQUIRED)

# Core library: everything except main(), so the unit tests can link it.
add_library(devdisc_core STATIC
  src/threading/thread_pool.cpp
  src/network/interface_discovery.cpp
  src/network/arp_discovery.cpp
  src/scanner/port_scanner.cpp
  src/scanner/ssh_detector.cpp
  src/parser/ifconfig_parser.cpp
  src/output/formatter.cpp
)
target_include_directories(devdisc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(devdisc_core PUBLIC Threads::Threads)
target_compile_options(devdisc_core PRIVATE -Wall -Wextra -Wpedantic)

# libssh2: preferred via pkg-config, with a plain find_library fallback.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBSSH2 IMPORTED_TARGET libssh2)
endif()

add_library(devdisc_ssh STATIC src/ssh/ssh_client.cpp)
target_include_directories(devdisc_ssh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(devdisc_ssh PUBLIC devdisc_core)
target_compile_options(devdisc_ssh PRIVATE -Wall -Wextra -Wpedantic)

if(LIBSSH2_FOUND)
  target_link_libraries(devdisc_ssh PUBLIC PkgConfig::LIBSSH2)
else()
  find_path(LIBSSH2_INCLUDE_DIR libssh2.h)
  find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2)
  if(NOT LIBSSH2_INCLUDE_DIR OR NOT LIBSSH2_LIBRARY)
    message(FATAL_ERROR
      "libssh2 development files not found. Install them, e.g.:\n"
      "  Debian/Ubuntu: sudo apt install libssh2-1-dev\n"
      "  Fedora/RHEL:   sudo dnf install libssh2-devel\n"
      "  Arch:          sudo pacman -S libssh2")
  endif()
  target_include_directories(devdisc_ssh PUBLIC ${LIBSSH2_INCLUDE_DIR})
  target_link_libraries(devdisc_ssh PUBLIC ${LIBSSH2_LIBRARY})
endif()

add_executable(device-discovery src/main.cpp)
target_link_libraries(device-discovery PRIVATE devdisc_ssh devdisc_core)
target_compile_options(device-discovery PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_subdirectory(tests)
