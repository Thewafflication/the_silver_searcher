/*
 * GNU-compatible getopt and getopt_long for the Windows build.
 *
 * ag's option table uses long options with required and optional arguments and
 * with flag pointers, and ag's users expect GNU argument permutation, so this
 * is a full reimplementation rather than a POSIX-only getopt.
 */

#ifndef AG_GETOPT_H
#define AG_GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

#define no_argument 0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name; /**< Long option spelling, without the leading dashes. */
    int has_arg;      /**< no_argument, required_argument or optional_argument. */
    int *flag;        /**< When non-null, receives val and getopt_long returns 0. */
    int val;          /**< Value stored through flag, or returned directly. */
};

/** Argument of the option just returned, or null when it has none. */
extern char *optarg;
/** Index of the next argv element to examine. */
extern int optind;
/** Non-zero (the default) prints diagnostics for unknown or incomplete options. */
extern int opterr;
/** The option character that caused the last '?' or ':' result. */
extern int optopt;

int getopt(int argc, char *const *argv, const char *shortopts);
int getopt_long(int argc, char *const *argv, const char *shortopts,
    const struct option *longopts, int *longindex);
int getopt_long_only(int argc, char *const *argv, const char *shortopts,
    const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#endif
