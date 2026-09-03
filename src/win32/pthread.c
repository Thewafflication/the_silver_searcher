/*
 * Win32 implementation of the pthread subset declared in pthread.h.
 *
 * Condition variables use Douglas Schmidt and Irfan Pyarali's semaphore and
 * broadcast-event construction, which needs nothing newer than Windows 2000.
 * ag always signals and broadcasts while holding the associated mutex, so a
 * waiter registers itself before releasing that mutex and cannot miss a wakeup.
 */

#include "pthread.h"
#include "ag_win32.h"

#include <errno.h>
#include <stdlib.h>
#include <windows.h>

/* The storage reserved in pthread.h must be able to hold a CRITICAL_SECTION. */
typedef char ag_critical_section_fits[
    (sizeof(CRITICAL_SECTION) <= sizeof(void *) * 8) ? 1 : -1];

#define AG_SYNC_UNINITIALIZED 0
#define AG_SYNC_READY 2

static CRITICAL_SECTION *ag_section(void *storage) {
    return (CRITICAL_SECTION *)storage;
}

/*
 * First-use initialization for a statically initialized object.
 *
 * The unlocked read of state is the fast path. It is sound here because ag
 * initializes every mutex and condition variable it shares between threads
 * before it creates any of them: either explicitly in main(), or implicitly
 * during single-threaded option parsing. A thread therefore only ever observes
 * a state that was already settled before CreateThread published its memory.
 */
static int ag_ensure_mutex(pthread_mutex_t *mutex) {
    if (mutex == NULL) {
        return EINVAL;
    }
    if (mutex->state == AG_SYNC_READY) {
        return 0;
    }
    ag_win32_global_lock();
    if (mutex->state != AG_SYNC_READY) {
        InitializeCriticalSection(ag_section(mutex->storage));
        mutex->state = AG_SYNC_READY;
    }
    ag_win32_global_unlock();
    return 0;
}

static int ag_ensure_cond(pthread_cond_t *condition) {
    int failed = 0;

    if (condition == NULL) {
        return EINVAL;
    }
    if (condition->state == AG_SYNC_READY) {
        return 0;
    }
    ag_win32_global_lock();
    if (condition->state != AG_SYNC_READY) {
        condition->waiters = 0;
        condition->broadcasting = 0;
        condition->semaphore = CreateSemaphoreA(NULL, 0, 0x7FFFFFFF, NULL);
        condition->waiters_done = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (condition->semaphore == NULL || condition->waiters_done == NULL) {
            if (condition->semaphore != NULL) {
                CloseHandle(condition->semaphore);
                condition->semaphore = NULL;
            }
            if (condition->waiters_done != NULL) {
                CloseHandle(condition->waiters_done);
                condition->waiters_done = NULL;
            }
            failed = 1;
        } else {
            InitializeCriticalSection(ag_section(condition->storage));
            condition->state = AG_SYNC_READY;
        }
    }
    ag_win32_global_unlock();
    return failed ? ENOMEM : 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes) {
    (void)attributes;
    if (mutex == NULL) {
        return EINVAL;
    }
    if (mutex->state == AG_SYNC_READY) {
        return 0;
    }
    return ag_ensure_mutex(mutex);
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (mutex == NULL) {
        return EINVAL;
    }
    ag_win32_global_lock();
    if (mutex->state == AG_SYNC_READY) {
        DeleteCriticalSection(ag_section(mutex->storage));
        mutex->state = AG_SYNC_UNINITIALIZED;
    }
    ag_win32_global_unlock();
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    int status = ag_ensure_mutex(mutex);
    if (status != 0) {
        return status;
    }
    EnterCriticalSection(ag_section(mutex->storage));
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    int status = ag_ensure_mutex(mutex);
    if (status != 0) {
        return status;
    }
    return TryEnterCriticalSection(ag_section(mutex->storage)) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (mutex == NULL || mutex->state != AG_SYNC_READY) {
        return EINVAL;
    }
    LeaveCriticalSection(ag_section(mutex->storage));
    return 0;
}

int pthread_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attributes) {
    (void)attributes;
    return ag_ensure_cond(condition);
}

int pthread_cond_destroy(pthread_cond_t *condition) {
    if (condition == NULL) {
        return EINVAL;
    }
    ag_win32_global_lock();
    if (condition->state == AG_SYNC_READY) {
        DeleteCriticalSection(ag_section(condition->storage));
        CloseHandle(condition->semaphore);
        CloseHandle(condition->waiters_done);
        condition->semaphore = NULL;
        condition->waiters_done = NULL;
        condition->state = AG_SYNC_UNINITIALIZED;
    }
    ag_win32_global_unlock();
    return 0;
}

int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex) {
    int status = ag_ensure_cond(condition);
    int last_waiter;

    if (status != 0) {
        return status;
    }
    status = ag_ensure_mutex(mutex);
    if (status != 0) {
        return status;
    }

    /* Register before releasing the caller's mutex, so no wakeup is lost. */
    EnterCriticalSection(ag_section(condition->storage));
    condition->waiters++;
    LeaveCriticalSection(ag_section(condition->storage));

    LeaveCriticalSection(ag_section(mutex->storage));
    WaitForSingleObject(condition->semaphore, INFINITE);

    EnterCriticalSection(ag_section(condition->storage));
    condition->waiters--;
    last_waiter = condition->broadcasting && condition->waiters == 0;
    LeaveCriticalSection(ag_section(condition->storage));

    /* The final waiter of a broadcast releases the broadcaster. */
    if (last_waiter) {
        SetEvent(condition->waiters_done);
    }

    EnterCriticalSection(ag_section(mutex->storage));
    return 0;
}

int pthread_cond_signal(pthread_cond_t *condition) {
    int status = ag_ensure_cond(condition);
    int have_waiters;

    if (status != 0) {
        return status;
    }
    EnterCriticalSection(ag_section(condition->storage));
    have_waiters = condition->waiters > 0;
    LeaveCriticalSection(ag_section(condition->storage));

    if (have_waiters) {
        ReleaseSemaphore(condition->semaphore, 1, NULL);
    }
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *condition) {
    int status = ag_ensure_cond(condition);
    long waiters;

    if (status != 0) {
        return status;
    }
    EnterCriticalSection(ag_section(condition->storage));
    waiters = condition->waiters;
    if (waiters > 0) {
        condition->broadcasting = 1;
        ReleaseSemaphore(condition->semaphore, waiters, NULL);
        LeaveCriticalSection(ag_section(condition->storage));
        /* Wait for every released waiter to leave before reusing the state. */
        WaitForSingleObject(condition->waiters_done, INFINITE);
        condition->broadcasting = 0;
    } else {
        LeaveCriticalSection(ag_section(condition->storage));
    }
    return 0;
}

typedef struct {
    void *(*routine)(void *);
    void *argument;
} ag_thread_start;

static DWORD WINAPI ag_thread_entry(LPVOID parameter) {
    ag_thread_start start = *(ag_thread_start *)parameter;
    free(parameter);
    start.routine(start.argument);
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
    void *(*start_routine)(void *), void *argument) {
    ag_thread_start *start;
    HANDLE handle;

    (void)attributes;
    if (thread == NULL || start_routine == NULL) {
        return EINVAL;
    }
    start = (ag_thread_start *)malloc(sizeof(*start));
    if (start == NULL) {
        return ENOMEM;
    }
    start->routine = start_routine;
    start->argument = argument;

    handle = CreateThread(NULL, 0, ag_thread_entry, start, 0, NULL);
    if (handle == NULL) {
        free(start);
        return EAGAIN;
    }
    *thread = (pthread_t)handle;
    return 0;
}

int pthread_join(pthread_t thread, void **result) {
    if (thread == NULL) {
        return EINVAL;
    }
    if (WaitForSingleObject((HANDLE)thread, INFINITE) != WAIT_OBJECT_0) {
        return EINVAL;
    }
    if (result != NULL) {
        /* ag's workers return nothing; a joined result is always null. */
        *result = NULL;
    }
    CloseHandle((HANDLE)thread);
    return 0;
}

int pthread_detach(pthread_t thread) {
    if (thread == NULL) {
        return EINVAL;
    }
    CloseHandle((HANDLE)thread);
    return 0;
}

void pthread_exit(void *result) {
    (void)result;
    ExitThread(0);
}

pthread_t pthread_self(void) {
    return (pthread_t)GetCurrentThread();
}
