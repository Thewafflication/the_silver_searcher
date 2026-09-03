/*
 * POSIX interfaces ag needs that the Waughtal C Run Time does not provide.
 *
 * This header is force-included into every ag translation unit by the build
 * (-include ag_win32.h), so ag's own sources need no Windows-specific edits.
 * It deliberately does not include <windows.h>: only ag_win32.c does, which
 * keeps the Win32 macro namespace out of the rest of the program.
 */

#ifndef AG_WIN32_H
#define AG_WIN32_H

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef AG_SSIZE_T_DEFINED
#define AG_SSIZE_T_DEFINED
/* Signed size, exactly as wide as a pointer on every target we build. */
typedef ptrdiff_t ssize_t;
#endif

#ifndef isascii
#define isascii(c) (((unsigned int)(c)) < 128U)
#endif

/*
 * WCRT's <errno.h> covers the C standard set. ag also tests for ENOTDIR, whose
 * value here matches Microsoft's <errno.h> so a mixed link keeps one meaning.
 */
#ifndef ENOTDIR
#define ENOTDIR 20
#endif
#ifndef EBUSY
#define EBUSY 16
#endif

/* WCRT's <sys/stat.h> stops at S_ISDIR and S_ISREG. */
#ifndef S_IFIFO
#define S_IFIFO 0x1000U
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#endif
#ifndef S_IFCHR
#define S_IFCHR 0x2000U
#endif
#ifndef S_ISCHR
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#endif

/*
 * ag's entry point is renamed so this layer can initialize its locks while the
 * process is still single-threaded. ag_win32.c defines the real main(), which
 * runs the startup and then calls ag's. ag_win32.c undefines this first.
 */
#define main ag_main

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the descriptor, popen and one-time-init locks. Idempotent. */
void ag_win32_startup(void);

/*
 * A process-wide lock the threading layer uses to serialize the first use of a
 * statically initialized mutex or condition variable. It exists before any
 * thread is created, so it never needs lazy initialization itself.
 */
void ag_win32_global_lock(void);
void ag_win32_global_unlock(void);

/*
 * Descriptor-based file access.
 *
 * These descriptors are ag's own: open() returns values from a private range
 * that cannot collide with the descriptors WCRT assigns to FILE streams, and
 * only the three standard descriptors (0, 1, 2) are shared between the two
 * numbering schemes.
 */
int open(const char *path, int flags, ...);
int close(int descriptor);
ssize_t read(int descriptor, void *buffer, size_t count);
int fstat(int descriptor, struct stat *result);
int isatty(int descriptor);

/* The descriptor for a WCRT stream; 0, 1 and 2 for the standard streams. */
int fileno(FILE *stream);

/*
 * The Windows handle behind a descriptor, or INVALID_HANDLE_VALUE ((void *)-1)
 * when the descriptor did not come from open() and is not a standard stream.
 * Callers treat the invalid result as "not a console", which is what ag's
 * colour detection wants for a pipe or a redirected stream.
 */
void *_get_osfhandle(int descriptor);

/*
 * Reads a child process's standard output. The child runs to completion and
 * its output is captured to a temporary file, which pclose() removes; ag only
 * uses popen() to read `git config`, so the difference from a live pipe is not
 * observable. Only the "r" mode is supported.
 */
FILE *popen(const char *command, const char *mode);
int pclose(FILE *stream);

/* Resolves path to an absolute Windows path, allocating when resolved is NULL. */
char *realpath(const char *path, char *resolved);

int gettimeofday(struct timeval *now, void *timezone_unused);

#ifdef __cplusplus
}
#endif

#endif
