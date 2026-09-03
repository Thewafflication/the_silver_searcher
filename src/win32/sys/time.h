/* struct timeval and struct timezone for the Windows build. */

#ifndef AG_SYS_TIME_H
#define AG_SYS_TIME_H

#include <time.h>

#ifndef AG_TIMEVAL_DEFINED
#define AG_TIMEVAL_DEFINED
struct timeval {
    long tv_sec;  /**< Whole seconds since the Unix epoch. */
    long tv_usec; /**< Microseconds, 0 through 999999. */
};
#endif

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

#endif
