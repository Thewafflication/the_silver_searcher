# Missing POSIX surface found porting The Silver Searcher to WCRT

## Context

I ported [The Silver Searcher](https://github.com/Thewafflication/the_silver_searcher)
(`ag`) to TinyCC + WCRT and packaged it for WPM. It now compiles and links for
x86, x64 and ARM64, and the resulting `ag.exe` imports only `kernel32.dll` and
`shlwapi.dll` — no `msvcrt`, no `wcrt.dll` — so the static link and the C89/C99
surface did exactly what they promise.

Everything below is what `ag` needed and WCRT does not have. All of it now lives
in `src/win32/` in that repository (roughly 1,100 lines), which is a reasonable
place for one program's shims but a poor place for facilities every ported
program will need. Filing it as one issue so the gaps are visible together;
happy to split it into per-area issues if that suits the roadmap better.

**Versions audited:** the released `wcrt-any-1.1.5.zip` package (headers and
`x64/lib/libwcrt.a`), TinyCC `0.9.28rc` from `tcc_package`.

The audit is against the release, not `master`. Three commits landed after the
`1.1.5` tag (`d7aa951`, `16497f1`, `66782de`) adding `_unlink`, `_access` and
directory creation, so a few items may already be closed.

---

## 1. Descriptor-based I/O

The largest gap, and the one that shaped everything else. WCRT has no
`open`/`close`/`read`/`write`/`lseek`/`fstat`, no `isatty`, no public `fileno`,
and — critically — **no way to get from a `FILE *` or a descriptor to a Windows
`HANDLE`**.

That last point is what forces a port to build its own descriptor table rather
than extend WCRT's. `ag` needs the `HANDLE` for two things a stream cannot
express: `CreateFileMapping` to search a file by memory-mapping it, and
`GetConsoleMode`/`SetConsoleTextAttribute` for colour output on consoles that
predate ANSI support.

| Missing | Notes |
| --- | --- |
| `open`, `close`, `read`, `write`, `lseek` | no `<fcntl.h>`, so no `O_RDONLY` either |
| `fstat` | `stat(path, ...)` exists; the descriptor form does not |
| `isatty` | needed to decide whether to colourize |
| `fileno` | `_fileno` exists and is public; the POSIX spelling is not |
| `_get_osfhandle` | no supported route from a stream or descriptor to a `HANDLE` |
| `fdopen` | see below |
| `dup`, `dup2` | not needed by `ag`, noting for completeness |

`_fileno` already returns 0, 1 and 2 for the standard streams and
`(stream - wcrt_streams) + 3` otherwise, so the numbering a descriptor layer
would need is half-built already.

**`fdopen` deserves separate mention.** Without it there is no way to wrap a
descriptor in a WCRT stream, so a program that opens something by descriptor and
then wants stream I/O over it is stuck. In `ag` this is why searching a named
pipe is disabled on Windows: the code path exists, is correct, and cannot be
compiled.

**Workaround:** a private descriptor table (`src/win32/ag_win32.c`) mapping ints
≥ 4096 to `HANDLE`s, with `open`/`close`/`read`/`fstat`/`isatty`/
`_get_osfhandle` on top, and descriptors 0–2 resolved through `GetStdHandle` so
they interoperate with `_fileno`.

## 2. Processes and pipes

`popen` / `pclose` are absent. `system` is present, but it cannot return output.

**Workaround:** run the child with `CreateProcessA`, redirect its stdout to a
temporary file, and `fopen` that. Fine for `ag`, which only reads
`git config --get core.excludesfile` and does not need to interleave with a live
child, but it is not `popen` semantics and would not survive a streaming use.

## 3. Threads

No `<pthread.h>` and no threading facility of any kind.

This is more than a naming gap on WCRT's stated floor. Windows 2000 has no
`SleepConditionVariableCS` (Vista) and no `InitOnceExecuteOnce` (Vista), so a
correct condition variable needs the semaphore-plus-broadcast-event
construction, and lazy initialization of a statically initialized mutex needs
either an interlocked guard or an ordering guarantee from the program.

**Workaround:** `src/win32/pthread.{h,c}` implements the twelve calls `ag` uses
on `CRITICAL_SECTION` plus that construction. It is correct for `ag` because
`ag` initializes every shared object before creating any thread; it is not a
general-purpose pthreads.

Whether this belongs in WCRT at all is a real question — it is POSIX, not C, and
C11 `<threads.h>` may be the better target. Recording it because it is a hard
blocker for porting anything concurrent, and every port will otherwise write its
own.

## 4. Paths

| Missing | Notes |
| --- | --- |
| `realpath` | POSIX |
| `_fullpath` | the Microsoft spelling is also absent |
| `getcwd` | |
| `PATH_MAX` / `MAX_PATH` | `<limits.h>` defines neither |

**Workaround:** `realpath` over `GetFullPathNameA`, which already normalizes
forward slashes and relative components.

## 5. Time

`gettimeofday`, `struct timeval` and `<sys/time.h>` are all absent. `<time.h>`
covers C89 but nothing sub-second, so a program cannot time itself.

**Workaround:** `struct timeval` in a local `<sys/time.h>`, and `gettimeofday`
over `GetSystemTimeAsFileTime`.

## 6. Option parsing

`<getopt.h>`, `getopt` and `getopt_long` are absent. POSIX rather than C, but
close to universal in ported command-line programs.

**Workaround:** a full GNU-compatible `getopt_long` in `src/win32/getopt.c`,
including argument permutation and unambiguous long-option abbreviation.
Verified byte-identical to glibc across 19 scenarios.

## 7. Strings and character classification

| Missing | Notes |
| --- | --- |
| `strndup` | `strdup` and `_strdup` are present |
| `isascii` | absent from `<ctype.h>` |
| `strlcpy`, `strlcat` | BSD, lowest priority — noting them because ported code reaches for them |

## 8. `errno` codes

`<errno.h>` carries 15 codes. Absent and wanted by ordinary filesystem code:

`EEXIST` (17), `ENOTDIR` (20), `EBUSY` (16), `EBADF` (9), `ENOTEMPTY` (41),
`EPIPE` (32), `ESPIPE` (29), `EISDIR` (21), `ENOSPC` (28)

The values above are Microsoft's, which match the ones WCRT already uses for the
codes it does define.

This one bit twice, in a way worth calling out: `EEXIST` is absent in 1.1.5 but
present on `master`, so code that compiles against a development tree fails
against the released package. Whatever the final set is, pinning it and treating
it as part of the release contract would help.

**Workaround:** define every code used, guarded by `#ifndef`, at the Microsoft
values, so the build no longer depends on which WCRT is installed.

## 9. `<dirent.h>`

`opendir` / `readdir` / `closedir` / `rewinddir` work and were a pleasure to use.
The limits:

- **`struct dirent` has no `d_reclen` or `d_namlen`.** Portable `scandir`
  implementations copy an entry with `malloc(entry->d_reclen)`; without it they
  need a Windows-specific branch. `d_reclen = sizeof(struct dirent)` for a
  fixed-size record would satisfy them.
- **`d_type` only ever reports `DT_UNKNOWN`, `DT_DIR` or `DT_REG`.** No
  `DT_LNK`, `DT_FIFO`, `DT_SOCK` or `DT_CHR`, so a program cannot classify a
  reparse point from `readdir` and must `stat` every entry. `DT_LNK` for
  `FILE_ATTRIBUTE_REPARSE_POINT` looks cheap — the attribute is already in the
  `WIN32_FIND_DATA`.
- No `scandir`, `alphasort`, `seekdir`, `telldir`, `dirfd`.

## 10. `<sys/stat.h>`

- **`stat()` always reports `st_ino == 0`.** `nFileIndexHigh`/`nFileIndexLow`
  from `GetFileInformationByHandle` would give real identity. `ag` uses inode
  equality to avoid searching the file its own output is redirected into; with a
  constant 0 that check has to be disabled, or every file compares equal to the
  output file.
- **No `lstat`.** Aliasing it to `stat` would let portable code compile;
  resolving reparse points properly would be better.
- **`S_ISFIFO`, `S_ISCHR`, `S_ISLNK`, `S_ISBLK`, `S_ISSOCK` are not defined.**
  `S_ISDIR` and `S_ISREG` are. `S_IFMT` is defined, so the rest are one line
  each, and `S_ISFIFO` in particular is reachable — `GetFileType` on a pipe
  handle returns `FILE_TYPE_PIPE`.

## 11. Headers absent entirely

`<fcntl.h>`, `<sys/time.h>`, `<sys/param.h>`, `<getopt.h>`, `<fnmatch.h>`,
`<pthread.h>`, `<sys/mman.h>`

`<fnmatch.h>` has a native answer — `PathMatchSpecA` in `shlwapi` — so a WCRT
`fnmatch` over it would remove a dependency that ports otherwise take on by
hand. (TinyCC ships no `shlwapi.h` or import definition either, so `ag` carries
both; that part belongs in `tcc_package`, not here.)

`<sys/mman.h>` is listed for completeness only. Memory mapping on Windows is
different enough that `CreateFileMapping` directly is the honest answer.

## 12. `<io.h>` is empty in 1.1.5

The released `include/io.h` contains only its include guard. `master` has since
added `_unlink` and `_access`. Not a defect on its own, but a header that exists
and declares nothing is easy to mistake for one that works — worth either
filling in or leaving out of the package.

---

## Suggested priority

If it helps to sequence this, from a porting point of view:

1. **Descriptor I/O plus a `HANDLE` accessor** (§1). Unblocks the most, and is
   the one gap a program genuinely cannot work around without duplicating
   WCRT's own bookkeeping.
2. **`errno` completeness** (§8) and the **`S_IS*` macros** (§10). Very cheap,
   and both currently cause build failures rather than degraded behaviour.
3. **`st_ino`** (§10) and **`d_type` / `d_reclen`** (§9). Small changes that
   turn "compiles but silently does less" into correct behaviour.
4. **Paths, time, `strndup`, `isascii`** (§4, §5, §7). Small and self-contained.
5. **`popen`** (§2), **`getopt`** (§6), **threads** (§3). Larger, and each is
   arguably its own decision about scope.

Every workaround named above is in
`Thewafflication/the_silver_searcher` under `src/win32/`, if any of it is useful
as a starting point or a test case.
