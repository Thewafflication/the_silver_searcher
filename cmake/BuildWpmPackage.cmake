# Stages ag into a WPM package and builds the .zip with wpm.exe.
#
# The package installs into %ProgramFiles%\Ag and puts that directory on the
# system Path indirectly, through an AG_HOME environment variable, so the Path
# entry stays short and stable across upgrades.
#
# Run with:
#   cmake -DAG_SOURCE_DIR=... -DAG_EXECUTABLE=... -DAG_VERSION=... \
#         -DAG_ARCH=... -DAG_STAGING_DIR=... -DAG_PACKAGE_OUTPUT_DIR=... \
#         -DAG_WPM_EXECUTABLE=... [-DAG_WPM_SIGNING_KEY=...] \
#         -P BuildWpmPackage.cmake

cmake_minimum_required(VERSION 3.21)

foreach(required
    AG_SOURCE_DIR AG_EXECUTABLE AG_VERSION AG_ARCH
    AG_STAGING_DIR AG_PACKAGE_OUTPUT_DIR AG_WPM_EXECUTABLE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "BuildWpmPackage.cmake requires ${required}")
  endif()
endforeach()

if(NOT AG_ARCH MATCHES "^(x86|x64|arm64)$")
  message(FATAL_ERROR "Unsupported WPM architecture: ${AG_ARCH}")
endif()
if(NOT EXISTS "${AG_EXECUTABLE}")
  message(FATAL_ERROR "ag.exe was not found: ${AG_EXECUTABLE}")
endif()

if(NOT DEFINED AG_PACKAGE_DEBUG)
  set(AG_PACKAGE_DEBUG false)
endif()
if(AG_PACKAGE_DEBUG)
  set(debug_metadata true)
  set(debug_filename "-debug")
else()
  set(debug_metadata false)
  set(debug_filename "")
endif()

# --------------------------------------------------------------------------
# Payload
# --------------------------------------------------------------------------

file(REMOVE_RECURSE "${AG_STAGING_DIR}")
file(MAKE_DIRECTORY "${AG_STAGING_DIR}/payload" "${AG_STAGING_DIR}/.wpm")

file(COPY "${AG_EXECUTABLE}" DESTINATION "${AG_STAGING_DIR}/payload")
if(DEFINED AG_WCRT_RUNTIME_DLL AND EXISTS "${AG_WCRT_RUNTIME_DLL}")
  # ag links WCRT statically, so this is only present for the case where a
  # future WCRT package ships an import library instead.
  file(COPY "${AG_WCRT_RUNTIME_DLL}" DESTINATION "${AG_STAGING_DIR}/payload")
endif()
foreach(document LICENSE NOTICE README.md)
  if(EXISTS "${AG_SOURCE_DIR}/${document}")
    file(COPY "${AG_SOURCE_DIR}/${document}" DESTINATION "${AG_STAGING_DIR}/payload")
  endif()
endforeach()
if(EXISTS "${AG_SOURCE_DIR}/doc/ag.1")
  file(COPY "${AG_SOURCE_DIR}/doc/ag.1" DESTINATION "${AG_STAGING_DIR}/payload/doc")
endif()

# --------------------------------------------------------------------------
# Metadata
# --------------------------------------------------------------------------

set(repository "https://github.com/Thewafflication/the_silver_searcher")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${AG_SOURCE_DIR}" config --get remote.origin.url
    OUTPUT_VARIABLE origin_url
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT origin_url STREQUAL "")
    set(repository "${origin_url}")
  endif()
endif()

string(TIMESTAMP build_date "%Y-%m-%d" UTC)

file(WRITE "${AG_STAGING_DIR}/.wpm/package.txt"
  "name=ag\n"
  "version=${AG_VERSION}\n"
  "arch=${AG_ARCH}\n"
  "debug=${debug_metadata}\n"
  "description=The Silver Searcher, a fast code-searching tool\n"
  "license=Apache-2.0\n"
  "build_date=${build_date}\n"
  "repo=${repository}\n"
  "homepage=${repository}\n"
  "maintainer=Jordan Waughtal\n"
)

# --------------------------------------------------------------------------
# Installation scripts
#
# Both scripts read the persistent Path out of the registry rather than using
# setx, which silently truncates a Path longer than 1024 characters. They write
# it back as REG_EXPAND_SZ containing the literal %AG_HOME% token, so the entry
# is one short indirection that survives a move of the installation directory.
# --------------------------------------------------------------------------

set(install_script "\
@echo off\r
setlocal EnableExtensions EnableDelayedExpansion\r
\r
if defined ProgramW6432 (set \"AG_DEST=%ProgramW6432%\\Ag\") else (set \"AG_DEST=%ProgramFiles%\\Ag\")\r
set \"AG_ENV_KEY=HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment\"\r
set \"AG_PAYLOAD=%~dp0..\\payload\"\r
set \"AG_BACKUP=%AG_DEST%.wpm-previous\"\r
\r
if not exist \"%AG_PAYLOAD%\\ag.exe\" (\r
    echo Error: the package payload does not contain ag.exe.\r
    exit /b 1\r
)\r
\r
rem Move any existing installation aside so a failure can be undone.\r
if exist \"%AG_BACKUP%\" rd /S /Q \"%AG_BACKUP%\" >nul 2>&1\r
if exist \"%AG_DEST%\" (\r
    move /Y \"%AG_DEST%\" \"%AG_BACKUP%\" >nul\r
    if errorlevel 1 (\r
        echo Error: could not replace the existing installation at \"%AG_DEST%\".\r
        echo Close any running ag.exe and run the installation again.\r
        exit /b 1\r
    )\r
)\r
\r
rem A rename is enough on one volume; fall back to copying across volumes.\r
move /Y \"%AG_PAYLOAD%\" \"%AG_DEST%\" >nul 2>&1\r
if errorlevel 1 (\r
    if not exist \"%AG_DEST%\" mkdir \"%AG_DEST%\"\r
    xcopy \"%AG_PAYLOAD%\\*\" \"%AG_DEST%\\\" /E /I /Q /Y >nul\r
    if errorlevel 1 (\r
        echo Error: could not install ag to \"%AG_DEST%\".\r
        goto :restore\r
    )\r
)\r
\r
echo Registering AG_HOME...\r
reg add \"%AG_ENV_KEY%\" /v AG_HOME /t REG_SZ /d \"%AG_DEST%\" /f >nul\r
if errorlevel 1 (\r
    echo Error: could not set the AG_HOME environment variable.\r
    echo Run the installation from an elevated command prompt.\r
    goto :restore\r
)\r
\r
set \"AG_PATH=\"\r
for /f \"tokens=1,2,*\" %%A in ('reg query \"%AG_ENV_KEY%\" /v Path 2^>nul ^| find /I \"Path\"') do set \"AG_PATH=%%C\"\r
if not defined AG_PATH (\r
    echo Error: could not read the persistent system Path.\r
    goto :restore\r
)\r
echo;!AG_PATH!;| findstr /I /C:\"%%AG_HOME%%\" >nul\r
if errorlevel 1 (\r
    reg add \"%AG_ENV_KEY%\" /v Path /t REG_EXPAND_SZ /d \"!AG_PATH!;%%AG_HOME%%\" /f >nul\r
    if errorlevel 1 (\r
        echo Error: could not add %%AG_HOME%% to the system Path.\r
        echo Run the installation from an elevated command prompt.\r
        goto :restore\r
    )\r
    echo Added %%AG_HOME%% to the system Path.\r
) else (\r
    echo The system Path already contains %%AG_HOME%%; no registry write is needed.\r
)\r
\r
if exist \"%AG_BACKUP%\" rd /S /Q \"%AG_BACKUP%\" >nul 2>&1\r
echo ag installed to \"%AG_DEST%\".\r
echo Open a new command prompt to use ag.\r
exit /b 0\r
\r
:restore\r
if exist \"%AG_DEST%\" rd /S /Q \"%AG_DEST%\" >nul 2>&1\r
if exist \"%AG_BACKUP%\" move /Y \"%AG_BACKUP%\" \"%AG_DEST%\" >nul 2>&1\r
exit /b 1\r
")

set(remove_script "\
@echo off\r
setlocal EnableExtensions EnableDelayedExpansion\r
\r
if defined ProgramW6432 (set \"AG_DEST=%ProgramW6432%\\Ag\") else (set \"AG_DEST=%ProgramFiles%\\Ag\")\r
set \"AG_ENV_KEY=HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment\"\r
\r
rem Only touch machine state if AG_HOME still names this installation.\r
set \"AG_CURRENT=\"\r
for /f \"tokens=1,2,*\" %%A in ('reg query \"%AG_ENV_KEY%\" /v AG_HOME 2^>nul ^| find /I \"AG_HOME\"') do set \"AG_CURRENT=%%C\"\r
\r
if exist \"%AG_DEST%\" (\r
    rd /S /Q \"%AG_DEST%\"\r
    if exist \"%AG_DEST%\" (\r
        echo Error: could not remove \"%AG_DEST%\".\r
        echo Close any running ag.exe and run the removal again.\r
        exit /b 1\r
    )\r
)\r
\r
if /I not \"%AG_CURRENT%\"==\"%AG_DEST%\" (\r
    echo AG_HOME points elsewhere; leaving the system Path unchanged.\r
    exit /b 0\r
)\r
\r
set \"AG_PATH=\"\r
for /f \"tokens=1,2,*\" %%A in ('reg query \"%AG_ENV_KEY%\" /v Path 2^>nul ^| find /I \"Path\"') do set \"AG_PATH=%%C\"\r
if defined AG_PATH (\r
    rem Remove only the %%AG_HOME%% token, with whichever separator it carries.\r
    set \"AG_NEW_PATH=!AG_PATH:;%%AG_HOME%%=!\"\r
    set \"AG_NEW_PATH=!AG_NEW_PATH:%%AG_HOME%%;=!\"\r
    set \"AG_NEW_PATH=!AG_NEW_PATH:%%AG_HOME%%=!\"\r
    if not \"!AG_NEW_PATH!\"==\"!AG_PATH!\" (\r
        reg add \"%AG_ENV_KEY%\" /v Path /t REG_EXPAND_SZ /d \"!AG_NEW_PATH!\" /f >nul\r
        if errorlevel 1 (\r
            echo Error: could not remove %%AG_HOME%% from the system Path.\r
            exit /b 1\r
        )\r
        echo Removed %%AG_HOME%% from the system Path.\r
    )\r
)\r
\r
reg delete \"%AG_ENV_KEY%\" /v AG_HOME /f >nul 2>&1\r
echo ag removed.\r
exit /b 0\r
")

file(WRITE "${AG_STAGING_DIR}/.wpm/install.cmd" "${install_script}")
file(WRITE "${AG_STAGING_DIR}/.wpm/remove.cmd" "${remove_script}")

# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------

file(MAKE_DIRECTORY "${AG_PACKAGE_OUTPUT_DIR}")
set(command "${AG_WPM_EXECUTABLE}" build "${AG_STAGING_DIR}" "${AG_PACKAGE_OUTPUT_DIR}")
if(DEFINED AG_WPM_SIGNING_KEY AND NOT "${AG_WPM_SIGNING_KEY}" STREQUAL "")
  if(NOT EXISTS "${AG_WPM_SIGNING_KEY}")
    message(FATAL_ERROR "The WPM signing key was not found: ${AG_WPM_SIGNING_KEY}")
  endif()
  list(APPEND command --sign "${AG_WPM_SIGNING_KEY}")
endif()

execute_process(
  COMMAND ${command}
  RESULT_VARIABLE status
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "wpm build failed:\n${output}${errors}")
endif()
message(STATUS "${output}")
message(STATUS
  "Built ${AG_PACKAGE_OUTPUT_DIR}/ag-${AG_ARCH}${debug_filename}-${AG_VERSION}.zip")
