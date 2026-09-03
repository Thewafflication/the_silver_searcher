/*
 * The subset of POSIX threads that ag uses, on Win32 primitives available
 * since Windows 2000.
 *
 * Deliberately not implemented: anything ag does not call. Condition variables
 * use the semaphore-and-broadcast-event construction rather than
 * SleepConditionVariableCS, which arrived in Windows Vista.
 *
 * Static initializers work because every mutex and condition variable
 * initializes itself on first use. Both are safe to initialize concurrently.
 */

#ifndef AG_PTHREAD_H
#define AG_PTHREAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An opaque Win32 HANDLE; declared as void * so <windows.h> is not needed. */
typedef void *pthread_t;

typedef struct {
    /* 0 uninitialized, 1 initializing, 2 ready. */
    volatile long state;
    /* Storage for a CRITICAL_SECTION, sized for every target we build. */
    void *storage[8];
} pthread_mutex_t;

typedef struct {
    volatile long state;
    void *storage[8]; /* CRITICAL_SECTION guarding the fields below. */
    long waiters;
    void *semaphore;
    void *waiters_done;
    int broadcasting;
} pthread_cond_t;

typedef struct {
    int unused;
} pthread_mutexattr_t;

typedef struct {
    int unused;
} pthread_condattr_t;

typedef struct {
    int unused;
} pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER { 0, { 0 } }
#define PTHREAD_COND_INITIALIZER { 0, { 0 }, 0, NULL, NULL, 0 }

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attributes);
int pthread_cond_destroy(pthread_cond_t *condition);
int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex);
int pthread_cond_signal(pthread_cond_t *condition);
int pthread_cond_broadcast(pthread_cond_t *condition);

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
    void *(*start_routine)(void *), void *argument);
int pthread_join(pthread_t thread, void **result);
int pthread_detach(pthread_t thread);
void pthread_exit(void *result);
pthread_t pthread_self(void);

#ifdef __cplusplus
}
#endif

#endif
