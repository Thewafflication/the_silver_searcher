/*
 * The one Shell Lightweight Utility entry point ag uses. TinyCC's bundled
 * Windows headers do not cover shlwapi, and ag only needs PathMatchSpec for
 * its ignore-pattern matching, so the declaration is kept minimal here and
 * the import is resolved through src/win32/shlwapi.def.
 */

#ifndef AG_SHLWAPI_H
#define AG_SHLWAPI_H

#ifdef __cplusplus
extern "C" {
#endif

int __stdcall PathMatchSpecA(const char *file, const char *spec);

#ifdef __cplusplus
}
#endif

#define PathMatchSpec PathMatchSpecA

#endif
