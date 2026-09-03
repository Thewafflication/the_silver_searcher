# The Silver Searcher, built for Windows with TinyCC and the Waughtal C Run
# Time and packaged with the Waughtal Package Manager.
#
# Configure through the checked-in presets, which select the architecture and
# the TinyCC toolchain file:
#
#   cmake --preset x64-release
#   cmake --build --preset x64-release

cmake_minimum_required(VERSION 3.21)

project(ag C)

if(NOT WIN32)
  message(FATAL_ERROR
    "This CMake build targets Windows only. On Unix, use ./build.sh."
  )
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
  message(FATAL_ERROR
    "ag's Windows build requires TinyCC. Configure through a checked-in preset."
  )
endif()

if(DEFINED AG_ARCH)
  set(ag_arch "${AG_ARCH}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]64|^[Aa][Aa][Rr][Cc][Hh]64")
  set(ag_arch "arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ag_arch "x64")
else()
  set(ag_arch "x86")
endif()
message(STATUS "ag target architecture: ${ag_arch}")

# --------------------------------------------------------------------------
# WCRT
# --------------------------------------------------------------------------
# The layout comes from the WCRT WPM package: shared headers in include/, and
# per-architecture libraries under x86/, x64/ and arm64/.

if(DEFINED AG_WCRT_ROOT AND NOT "${AG_WCRT_ROOT}" STREQUAL "")
  file(TO_CMAKE_PATH "${AG_WCRT_ROOT}" AG_WCRT_ROOT)
elseif(DEFINED ENV{AG_WCRT_ROOT} AND NOT "$ENV{AG_WCRT_ROOT}" STREQUAL "")
  file(TO_CMAKE_PATH "$ENV{AG_WCRT_ROOT}" AG_WCRT_ROOT)
elseif(DEFINED ENV{WPM_WCRT_ROOT} AND NOT "$ENV{WPM_WCRT_ROOT}" STREQUAL "")
  # The shared setup-tinycc action publishes the resolved root under this name.
  file(TO_CMAKE_PATH "$ENV{WPM_WCRT_ROOT}" AG_WCRT_ROOT)
else()
  file(TO_CMAKE_PATH "$ENV{ProgramFiles}/WCRT" ag_wcrt_install_root)
  file(GLOB ag_wcrt_installations LIST_DIRECTORIES TRUE "${ag_wcrt_install_root}/*")
  list(SORT ag_wcrt_installations COMPARE NATURAL ORDER DESCENDING)
  foreach(candidate IN LISTS ag_wcrt_installations)
    if(EXISTS "${candidate}/include/stdio.h" AND
       EXISTS "${candidate}/${ag_arch}/lib/libwcrt.a" AND
       EXISTS "${candidate}/${ag_arch}/lib/wcrt-startup-console.o")
      set(AG_WCRT_ROOT "${candidate}")
      break()
    endif()
  endforeach()
endif()

if(NOT AG_WCRT_ROOT)
  message(FATAL_ERROR
    "WCRT was not found. Install the wcrt WPM package or set AG_WCRT_ROOT."
  )
endif()

set(ag_wcrt_target_root "${AG_WCRT_ROOT}/${ag_arch}")
foreach(required
    "include/stdio.h"
    "include/dirent.h"
    "${ag_arch}/lib/libwcrt.a"
    "${ag_arch}/lib/wcrt-startup-console.o")
  if(NOT EXISTS "${AG_WCRT_ROOT}/${required}")
    message(FATAL_ERROR
      "The WCRT SDK at ${AG_WCRT_ROOT} is missing ${required}."
    )
  endif()
endforeach()

set(AG_WCRT_VERSION "unknown")
if(EXISTS "${AG_WCRT_ROOT}/.wpm/package.txt")
  file(STRINGS "${AG_WCRT_ROOT}/.wpm/package.txt" ag_wcrt_version_line REGEX "^version=")
  if(ag_wcrt_version_line)
    list(GET ag_wcrt_version_line 0 ag_wcrt_version_line)
    string(REGEX REPLACE "^version=" "" AG_WCRT_VERSION "${ag_wcrt_version_line}")
  endif()
endif()
message(STATUS "Using WCRT ${AG_WCRT_VERSION} from ${AG_WCRT_ROOT}")

# WCRT's headers must shadow the ones TinyCC bundles.
include_directories(BEFORE "${AG_WCRT_ROOT}/include")

# --------------------------------------------------------------------------
# Version
# --------------------------------------------------------------------------

set(ag_version_header "${CMAKE_BINARY_DIR}/generated/version.h")
set(ag_version_file "${CMAKE_BINARY_DIR}/generated/version.txt")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DAG_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DAG_VERSION_HEADER=${ag_version_header}"
    "-DAG_VERSION_FILE=${ag_version_file}"
    -P "${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake"
  COMMAND_ERROR_IS_FATAL ANY
)
file(READ "${ag_version_file}" AG_VERSION)
string(STRIP "${AG_VERSION}" AG_VERSION)
message(STATUS "ag version: ${AG_VERSION}")

# Reconfigure when HEAD moves, so a rebuilt package never reports a stale
# version.
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/.git/HEAD")
endif()

# --------------------------------------------------------------------------
# PCRE
# --------------------------------------------------------------------------

add_subdirectory(third_party)

# --------------------------------------------------------------------------
# ag
# --------------------------------------------------------------------------

set(ag_sources
  src/decompress.c
  src/ignore.c
  src/lang.c
  src/log.c
  src/main.c
  src/options.c
  src/print.c
  src/print_w32.c
  src/scandir.c
  src/search.c
  src/util.c
  src/win32/ag_win32.c
  src/win32/getopt.c
  src/win32/pthread.c
)

add_executable(ag ${ag_sources})

target_include_directories(ag PRIVATE
  "${CMAKE_SOURCE_DIR}/src/win32"
  "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(ag PRIVATE pcre)

set_property(TARGET ag PROPERTY C_STANDARD 99)
set_property(TARGET ag PROPERTY C_STANDARD_REQUIRED ON)

target_compile_definitions(ag PRIVATE
  "PACKAGE_VERSION=\"${AG_VERSION}\""
  AG_TARGET_ARCH="${ag_arch}"
  _WINT_T
  WCRT_POSIX=1
)

# ag_win32.h supplies the POSIX surface WCRT lacks to every ag translation
# unit, so ag's own sources stay unmodified. It includes <wchar.h> first, which
# is what lets WCRT establish time_t and wint_t before TinyCC's WinAPI headers
# see them.
#
# This must stay a single -include: CMake removes duplicate tokens from
# COMPILE_OPTIONS, so a second "-include" would be dropped and its path handed
# to TinyCC as a stray input file.
target_compile_options(ag PRIVATE
  -include "${CMAKE_SOURCE_DIR}/src/win32/ag_win32.h"
)

# CMake's TinyCC support already adds -g for a Debug build; this only records
# the flavour for the package metadata and filename.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(ag_package_debug true)
else()
  set(ag_package_debug false)
endif()

# Explicit import definitions keep a cross-architecture build from depending on
# matching system DLLs being present on the build host.
target_link_options(ag PRIVATE
  -nostdlib
  "${AG_TCC_ROOT}/lib/kernel32.def"
  "${CMAKE_SOURCE_DIR}/src/win32/shlwapi.def"
)

set(ag_startup "${ag_wcrt_target_root}/lib/wcrt-startup-console.o")
set_source_files_properties("${ag_startup}" PROPERTIES EXTERNAL_OBJECT TRUE)
target_sources(ag PRIVATE "${ag_startup}")
target_link_libraries(ag PRIVATE "${ag_wcrt_target_root}/lib/libwcrt.a")

get_filename_component(ag_tcc_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
if(ag_arch STREQUAL "x86")
  set(ag_tcc_runtime "${ag_tcc_bin_dir}/lib/i386-win32-libtcc1.a")
elseif(ag_arch STREQUAL "x64")
  set(ag_tcc_runtime "${ag_tcc_bin_dir}/lib/x86_64-win32-libtcc1.a")
else()
  set(ag_tcc_runtime "${ag_tcc_bin_dir}/lib/arm64-win32-libtcc1.a")
endif()
target_link_libraries(ag PRIVATE "${ag_tcc_runtime}")

set(ag_output_dir "${CMAKE_SOURCE_DIR}/bin/${ag_arch}/${CMAKE_BUILD_TYPE}")
set_target_properties(ag PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ag_output_dir}")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set_target_properties(ag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_SOURCE_DIR}/bin/${ag_arch}/${config}")
endforeach()

# --------------------------------------------------------------------------
# WPM package
# --------------------------------------------------------------------------

set(AG_WPM_EXECUTABLE "" CACHE FILEPATH
  "wpm.exe used to build the package; found on PATH when empty")
set(AG_WPM_SIGNING_KEY "" CACHE FILEPATH
  "Ed25519 private key used to sign the package; unsigned when empty")
set(AG_PACKAGE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/out/packages" CACHE PATH
  "Directory that receives the built .zip package")

if(AG_WPM_EXECUTABLE STREQUAL "")
  find_program(ag_wpm_program NAMES wpm.exe wpm)
else()
  set(ag_wpm_program "${AG_WPM_EXECUTABLE}")
endif()

if(ag_wpm_program)
  add_custom_target(wpm-package
    COMMAND "${CMAKE_COMMAND}"
      "-DAG_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
      "-DAG_EXECUTABLE=$<TARGET_FILE:ag>"
      "-DAG_WCRT_RUNTIME_DLL=${ag_wcrt_target_root}/bin/wcrt.dll"
      "-DAG_VERSION=${AG_VERSION}"
      "-DAG_ARCH=${ag_arch}"
      "-DAG_PACKAGE_DEBUG=${ag_package_debug}"
      "-DAG_STAGING_DIR=${CMAKE_BINARY_DIR}/package"
      "-DAG_PACKAGE_OUTPUT_DIR=${AG_PACKAGE_OUTPUT_DIR}"
      "-DAG_WPM_EXECUTABLE=${ag_wpm_program}"
      "-DAG_WPM_SIGNING_KEY=${AG_WPM_SIGNING_KEY}"
      -P "${CMAKE_SOURCE_DIR}/cmake/BuildWpmPackage.cmake"
    DEPENDS ag
    COMMENT "Building the ag WPM package"
    VERBATIM
  )
else()
  message(STATUS
    "wpm.exe was not found; the 'wpm-package' target is unavailable. "
    "Install WPM or set AG_WPM_EXECUTABLE."
  )
endif()
