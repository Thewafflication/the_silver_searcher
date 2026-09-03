/*
 * ag build configuration for Windows with TinyCC and the Waughtal C Run Time.
 *
 * The autotools configure script cannot run under this toolchain, so the
 * HAVE_* answers it would produce are recorded here. Each one is a statement
 * about WCRT plus the compatibility layer in ag_win32.h, not about the host.
 */

#ifndef AG_CONFIG_H
#define AG_CONFIG_H

/* Provided by src/win32/pthread.h. */
#define HAVE_PTHREAD_H 1

/* Provided by src/win32/ag_win32.c on top of GetFullPathNameA. */
#define HAVE_REALPATH 1

/*
 * Deliberately absent, so ag compiles its own portable fallbacks:
 *   HAVE_STRLCPY, HAVE_STRLCAT, HAVE_STRNDUP, HAVE_VASPRINTF,
 *   HAVE_GETLINE, HAVE_FGETLN
 *
 * Deliberately absent because the platform has no equivalent:
 *   HAVE_ZLIB_H, HAVE_LZMA_H  - compressed-file search (-z) is not built
 *   HAVE_ERR_H, HAVE_PLEDGE
 *   HAVE_PTHREAD_SETAFFINITY_NP, HAVE_SYS_CPUSET_H
 *   HAVE_DIRENT_DTYPE, HAVE_DIRENT_DNAMLEN - WCRT's readdir reports
 *       DT_UNKNOWN, DT_DIR and DT_REG only, so ag must stat() to classify
 *       FIFOs and sockets
 *   USE_PCRE_JIT - PCRE is built without SUPPORT_JIT
 */

/*
 * S_ISFIFO and S_ISCHR, which WCRT's <sys/stat.h> does not define, come from
 * src/win32/ag_win32.h. The build force-includes that header into every ag
 * translation unit, so it is in scope wherever this file is.
 */

#endif
