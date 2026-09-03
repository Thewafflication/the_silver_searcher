# Windows toolchain using the architecture-specific compiler in TinyCC's
# multi-architecture Windows package.
#
# Set AG_TCC_ROOT (CMake or environment) to use a TinyCC installation other
# than the newest one beneath %ProgramFiles%\TinyCC.

set(CMAKE_SYSTEM_NAME Windows)

if(NOT DEFINED AG_ARCH)
  set(AG_ARCH x86)
endif()

if(AG_ARCH STREQUAL "x86")
  set(AG_TCC_DRIVER i386-win32-tcc.exe)
  set(CMAKE_SYSTEM_PROCESSOR x86)
elseif(AG_ARCH STREQUAL "x64")
  set(AG_TCC_DRIVER x86_64-win32-tcc.exe)
  set(CMAKE_SYSTEM_PROCESSOR AMD64)
elseif(AG_ARCH STREQUAL "arm64")
  set(AG_TCC_DRIVER arm64-win32-tcc.exe)
  set(CMAKE_SYSTEM_PROCESSOR ARM64)
else()
  message(FATAL_ERROR "Unsupported TinyCC Windows architecture: ${AG_ARCH}")
endif()

if(DEFINED AG_TCC_ROOT AND NOT "${AG_TCC_ROOT}" STREQUAL "")
  file(TO_CMAKE_PATH "${AG_TCC_ROOT}" AG_TCC_ROOT)
elseif(DEFINED ENV{AG_TCC_ROOT} AND NOT "$ENV{AG_TCC_ROOT}" STREQUAL "")
  file(TO_CMAKE_PATH "$ENV{AG_TCC_ROOT}" AG_TCC_ROOT)
elseif(DEFINED ENV{WPM_TCC_ROOT} AND NOT "$ENV{WPM_TCC_ROOT}" STREQUAL "")
  # The shared setup-tinycc action publishes the resolved root under this name.
  file(TO_CMAKE_PATH "$ENV{WPM_TCC_ROOT}" AG_TCC_ROOT)
else()
  file(TO_CMAKE_PATH "$ENV{ProgramFiles}/TinyCC" AG_TCC_INSTALL_ROOT)
  file(GLOB AG_TCC_INSTALLATIONS LIST_DIRECTORIES TRUE "${AG_TCC_INSTALL_ROOT}/*")
  list(SORT AG_TCC_INSTALLATIONS COMPARE NATURAL ORDER DESCENDING)
  foreach(AG_TCC_INSTALLATION IN LISTS AG_TCC_INSTALLATIONS)
    if(EXISTS "${AG_TCC_INSTALLATION}/tcc.exe")
      set(AG_TCC_ROOT "${AG_TCC_INSTALLATION}")
      break()
    endif()
  endforeach()
endif()

if(NOT AG_TCC_ROOT OR NOT EXISTS "${AG_TCC_ROOT}/${AG_TCC_DRIVER}")
  message(FATAL_ERROR
    "TinyCC's ${AG_TCC_DRIVER} was not found under '${AG_TCC_ROOT}'. "
    "Install the multi-architecture TinyCC WPM package or set AG_TCC_ROOT."
  )
endif()

set(AG_TCC_ROOT "${AG_TCC_ROOT}" CACHE PATH "TinyCC installation root")
set(CMAKE_C_COMPILER "${AG_TCC_ROOT}/${AG_TCC_DRIVER}" CACHE FILEPATH "TinyCC compiler")

# TinyCC's PE linker emits a console application by default. Keep the flag
# explicit so the intended executable type stays visible in verbose builds.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-subsystem=console")
