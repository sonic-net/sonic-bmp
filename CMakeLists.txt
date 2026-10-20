# cmake file for openbmpd

# Define the version
set (OPENBMP_VER_MAJOR "0")
set (OPENBMP_VER_MINOR "14")
set (OPENBMP_VER_PATCH "0")
set (OPENBMP_VER_RELEASE "0")
set (OPENBMPD_VERSION "${OPENBMP_VER_MAJOR}.${OPENBMP_VER_MINOR}.${OPENBMP_VER_PATCH}-${OPENBMP_VER_RELEASE}")

cmake_minimum_required (VERSION 2.6)
project (OPENBMP)

option(BMP_PARSER_TESTS "Build offline BMP parser regression tests" OFF)
option(BMP_PARSER_TESTS_ONLY "Build offline parser tests without message-bus backends" OFF)
option(BMP_PARSER_SANITIZERS "Enable address and undefined-behavior sanitizers for parser tests" OFF)
if(BMP_PARSER_TESTS_ONLY)
    set(BMP_PARSER_TESTS ON)
endif()
if(BMP_PARSER_TESTS)
    enable_testing()
endif()

# find and include boost
find_package( Boost 1.41.0 REQUIRED)
   if(Boost_FOUND)
      include_directories(${Boost_INCLUDE_DIRS})
  endif()

# Configure the version for compiling
configure_file( openbmpd_version.h.in ${CMAKE_BINARY_DIR}/generated/openbmpd_version.h )
include_directories( ${CMAKE_BINARY_DIR}/generated/ ) 

if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(LINUX TRUE)
elseif (CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(MACOSX TRUE)
else ()
    Message (FATAL_ERROR "${CMAKE_SYSTEM_NAME} not supported; Must be Linux or Darwin")
endif()

# Add the Server directory
add_subdirectory (Server)

if(NOT BMP_PARSER_TESTS_ONLY)
FIND_PROGRAM(DPKG_CMD dpkg)
IF(DPKG_CMD)
  add_subdirectory (deb_package)
ENDIF(DPKG_CMD)

FIND_PROGRAM(RPKG_CMD rpmbuild)
IF(RPKG_CMD)
  add_subdirectory (rpm_package)
ENDIF(RPKG_CMD)
endif()
