# PCRE 8.45, built as an object library and linked statically into ag.exe.
#
# PCRE's own build systems cannot configure themselves under TinyCC, so this
# file supplies the generated pieces directly: config.h comes from
# pcre-config.h.in, pcre.h from PCRE's pcre.h.in, and the character tables from
# the C-locale pcre_chartables.c.dist that PCRE ships for exactly this case.

set(pcre_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/pcre")

if(NOT EXISTS "${pcre_source_dir}/pcre.h.in")
  message(FATAL_ERROR
    "third_party/pcre is empty. Run: git submodule update --init --recursive"
  )
endif()

# Kept in step with the pinned submodule commit (PCRE 8.45, 2021-06-15).
set(PCRE_MAJOR 8)
set(PCRE_MINOR 45)
set(PCRE_PRERELEASE "")
set(PCRE_DATE 2021-06-15)

set(pcre_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${pcre_generated_dir}")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/pcre-config.h.in"
  "${pcre_generated_dir}/config.h"
  COPYONLY
)
configure_file(
  "${pcre_source_dir}/pcre.h.in"
  "${pcre_generated_dir}/pcre.h"
  @ONLY
)
configure_file(
  "${pcre_source_dir}/pcre_chartables.c.dist"
  "${pcre_generated_dir}/pcre_chartables.c"
  COPYONLY
)

set(pcre_sources
  "${pcre_generated_dir}/pcre_chartables.c"
  "${pcre_source_dir}/pcre_byte_order.c"
  "${pcre_source_dir}/pcre_compile.c"
  "${pcre_source_dir}/pcre_config.c"
  "${pcre_source_dir}/pcre_dfa_exec.c"
  "${pcre_source_dir}/pcre_exec.c"
  "${pcre_source_dir}/pcre_fullinfo.c"
  "${pcre_source_dir}/pcre_get.c"
  "${pcre_source_dir}/pcre_globals.c"
  "${pcre_source_dir}/pcre_jit_compile.c"
  "${pcre_source_dir}/pcre_maketables.c"
  "${pcre_source_dir}/pcre_newline.c"
  "${pcre_source_dir}/pcre_ord2utf8.c"
  "${pcre_source_dir}/pcre_refcount.c"
  "${pcre_source_dir}/pcre_string_utils.c"
  "${pcre_source_dir}/pcre_study.c"
  "${pcre_source_dir}/pcre_tables.c"
  "${pcre_source_dir}/pcre_ucd.c"
  "${pcre_source_dir}/pcre_valid_utf8.c"
  "${pcre_source_dir}/pcre_version.c"
  "${pcre_source_dir}/pcre_xclass.c"
)

# An object library keeps the build off TinyCC's `tcc -ar` path entirely: the
# objects are handed straight to the ag link line.
add_library(pcre OBJECT ${pcre_sources})

target_compile_definitions(pcre PUBLIC PCRE_STATIC)
target_compile_definitions(pcre PRIVATE HAVE_CONFIG_H)

# PRIVATE: only PCRE's own translation units see its config.h, so it cannot
# collide with the ag config.h in src/win32.
target_include_directories(pcre PRIVATE "${pcre_generated_dir}")
target_include_directories(pcre PRIVATE "${pcre_source_dir}")
# INTERFACE: ag consumers get pcre.h only.
target_include_directories(pcre INTERFACE "${pcre_generated_dir}")

set_property(TARGET pcre PROPERTY C_STANDARD 99)

if(WIN32)
  target_compile_options(pcre PRIVATE -include wchar.h)
  target_compile_definitions(pcre PRIVATE _WINT_T WCRT_POSIX=1)
endif()
