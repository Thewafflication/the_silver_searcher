/*
 * The one Shell Lightweight Utility entry point ag uses. TinyCC's bundled
 * Windows headers do not cover shlwapi, and ag only needs PathMatchSpec for
 * its ignore-pattern matching, so the declaration is kept minimal here and
 * the import is resolved through src/win32/shlwapi.def.
 */

#ifndef AG_SHLWAPI_H
#define AG_SHLWAPI_H

/*
 * TinyCC does not accept a bare __stdcall between the return type and the
 * name; its own <_mingw.h> expands the keyword to this attribute form, which
 * parses on x86, x64 and ARM64 and is ignored on the two where there is no
 * separate stdcall convention.
 */
#if defined(__TINYC__) || defined(__GNUC__)
#define AG_STDCALL __attribute__((__stdcall__))
#else
#define AG_STDCALL __stdcall
#endif

#ifdef __cplusplus
extern "C" {
#endif

int AG_STDCALL PathMatchSpecA(const char *file, const char *spec);

#ifdef __cplusplus
}
#endif

#define PathMatchSpec PathMatchSpecA

#endif
