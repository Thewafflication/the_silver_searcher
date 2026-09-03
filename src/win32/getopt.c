/*
 * GNU-compatible getopt_long.
 *
 * Behaviour reproduced from the GNU C Library, because ag depends on it:
 *   - options and operands may be interleaved; operands are permuted to the
 *     end of argv so `ag -i pattern path` and `ag pattern path -i` agree
 *   - "--" ends option processing
 *   - a long option may be abbreviated to any unambiguous prefix
 *   - "--name=value", "--name value", "-ovalue" and "-o value" are all accepted
 *   - a leading ':' in the short-option string selects ':' instead of '?' for
 *     a missing argument and suppresses diagnostics
 */

#include "getopt.h"

#include <stdio.h>
#include <string.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

/* Position inside a cluster such as "-abc"; null between argv elements. */
static char *nextchar;

/*
 * Operands seen so far occupy [first_nonopt, last_nonopt), and options seen
 * since then occupy [last_nonopt, optind). Swapping the two blocks is what
 * moves operands past options as scanning proceeds.
 */
static int first_nonopt;
static int last_nonopt;
static int initialized;

static void reverse_range(char **argv, int from, int to) {
    while (from + 1 < to) {
        char *held = argv[from];
        argv[from] = argv[to - 1];
        argv[to - 1] = held;
        from++;
        to--;
    }
}

/* Rotates the operand block behind the option block. */
static void exchange(char **argv) {
    int bottom = first_nonopt;
    int middle = last_nonopt;
    int top = optind;

    reverse_range(argv, bottom, middle);
    reverse_range(argv, middle, top);
    reverse_range(argv, bottom, top);

    first_nonopt += top - middle;
    last_nonopt = top;
}

static int missing_argument_result(const char *shortopts) {
    return shortopts[0] == ':' ? ':' : '?';
}

static int report(const char *shortopts) {
    return opterr && shortopts[0] != ':';
}

static int parse_long(int argc, char *const *argv, const char *shortopts,
    const struct option *longopts, int *longindex) {
    char *name = argv[optind] + 2;
    char *equals = strchr(name, '=');
    size_t length = equals != NULL ? (size_t)(equals - name) : strlen(name);
    const struct option *found = NULL;
    int found_index = -1;
    int ambiguous = 0;
    int index;

    for (index = 0; longopts[index].name != NULL; index++) {
        if (strncmp(longopts[index].name, name, length) != 0) {
            continue;
        }
        if (strlen(longopts[index].name) == length) {
            /* Exact match always wins over any abbreviation. */
            found = &longopts[index];
            found_index = index;
            ambiguous = 0;
            break;
        }
        if (found == NULL) {
            found = &longopts[index];
            found_index = index;
        } else if (found->flag != longopts[index].flag ||
                   found->has_arg != longopts[index].has_arg ||
                   found->val != longopts[index].val) {
            ambiguous = 1;
        }
    }

    if (ambiguous) {
        if (report(shortopts)) {
            fprintf(stderr, "%s: option '--%.*s' is ambiguous\n", argv[0],
                (int)length, name);
        }
        nextchar = NULL;
        optind++;
        optopt = 0;
        return '?';
    }
    if (found == NULL) {
        if (report(shortopts)) {
            fprintf(stderr, "%s: unrecognized option '--%.*s'\n", argv[0],
                (int)length, name);
        }
        nextchar = NULL;
        optind++;
        optopt = 0;
        return '?';
    }

    optind++;
    if (equals != NULL) {
        if (found->has_arg == no_argument) {
            if (report(shortopts)) {
                fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
                    argv[0], found->name);
            }
            nextchar = NULL;
            optopt = found->val;
            return '?';
        }
        optarg = equals + 1;
    } else if (found->has_arg == required_argument) {
        if (optind >= argc) {
            if (report(shortopts)) {
                fprintf(stderr, "%s: option '--%s' requires an argument\n",
                    argv[0], found->name);
            }
            nextchar = NULL;
            optopt = found->val;
            return missing_argument_result(shortopts);
        }
        optarg = argv[optind++];
    }

    nextchar = NULL;
    if (longindex != NULL) {
        *longindex = found_index;
    }
    if (found->flag != NULL) {
        *found->flag = found->val;
        return 0;
    }
    return found->val;
}

static int parse_short(int argc, char *const *argv, const char *shortopts) {
    char option = *nextchar++;
    const char *position = strchr(shortopts, option);

    if (*nextchar == '\0') {
        optind++;
    }

    if (position == NULL || option == ':') {
        if (report(shortopts)) {
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], option);
        }
        optopt = option;
        if (*nextchar == '\0') {
            nextchar = NULL;
        }
        return '?';
    }

    if (position[1] != ':') {
        if (*nextchar == '\0') {
            nextchar = NULL;
        }
        return option;
    }

    if (position[2] == ':') {
        /* Optional argument: only an attached one counts, as in GNU getopt. */
        if (*nextchar != '\0') {
            optarg = nextchar;
            optind++;
        } else {
            optarg = NULL;
        }
        nextchar = NULL;
        return option;
    }

    if (*nextchar != '\0') {
        optarg = nextchar;
        optind++;
    } else if (optind >= argc) {
        if (report(shortopts)) {
            fprintf(stderr, "%s: option requires an argument -- '%c'\n",
                argv[0], option);
        }
        optopt = option;
        nextchar = NULL;
        return missing_argument_result(shortopts);
    } else {
        optarg = argv[optind++];
    }
    nextchar = NULL;
    return option;
}

static int getopt_internal(int argc, char *const *argv, const char *shortopts,
    const struct option *longopts, int *longindex) {
    char **mutable_argv = (char **)argv;

    if (shortopts == NULL) {
        shortopts = "";
    }
    optarg = NULL;

    if (optind == 0) {
        optind = 1;
        initialized = 0;
    }
    if (!initialized) {
        first_nonopt = optind;
        last_nonopt = optind;
        nextchar = NULL;
        initialized = 1;
    }
    if (shortopts[0] == '+' || shortopts[0] == '-') {
        /* Only GNU's default permuting mode is implemented. */
        shortopts++;
    }

    if (nextchar == NULL || *nextchar == '\0') {
        if (last_nonopt > optind) {
            last_nonopt = optind;
        }
        if (first_nonopt > optind) {
            first_nonopt = optind;
        }

        /* Move any operands already scanned behind the options after them. */
        if (first_nonopt != last_nonopt && last_nonopt != optind) {
            exchange(mutable_argv);
        } else if (last_nonopt != optind) {
            first_nonopt = optind;
        }

        while (optind < argc &&
               (argv[optind][0] != '-' || argv[optind][1] == '\0')) {
            optind++;
        }
        last_nonopt = optind;

        if (optind != argc && strcmp(argv[optind], "--") == 0) {
            optind++;
            if (first_nonopt != last_nonopt && last_nonopt != optind) {
                exchange(mutable_argv);
            } else if (first_nonopt == last_nonopt) {
                first_nonopt = optind;
            }
            last_nonopt = argc;
            optind = argc;
        }

        if (optind == argc) {
            /* Rewind to the first operand so the caller can read them. */
            if (first_nonopt != last_nonopt) {
                optind = first_nonopt;
            }
            initialized = 0;
            return -1;
        }

        if (longopts != NULL && argv[optind][1] == '-') {
            return parse_long(argc, argv, shortopts, longopts, longindex);
        }

        nextchar = mutable_argv[optind] + 1;
    }

    return parse_short(argc, argv, shortopts);
}

int getopt(int argc, char *const *argv, const char *shortopts) {
    return getopt_internal(argc, argv, shortopts, NULL, NULL);
}

int getopt_long(int argc, char *const *argv, const char *shortopts,
    const struct option *longopts, int *longindex) {
    return getopt_internal(argc, argv, shortopts, longopts, longindex);
}

/*
 * ag does not use this, and single-dash long options are not implemented, so
 * it behaves exactly like getopt_long. It exists only so a caller that expects
 * the GNU name still links.
 */
int getopt_long_only(int argc, char *const *argv, const char *shortopts,
    const struct option *longopts, int *longindex) {
    return getopt_internal(argc, argv, shortopts, longopts, longindex);
}
