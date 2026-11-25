#ifndef LAB3_PLATFORM_H
#define LAB3_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE Process;
#else
#include <sys/types.h>
typedef pid_t Process;
#endif

#define NO_PROCESS ((Process)0)
#define TIMESTAMP_SIZE 32

typedef struct {
    FILE *log;
    int64_t *counter;
} SharedState;

SharedState platform_open(void);
void platform_close(void);
void platform_lock(void);
void platform_unlock(void);

bool platform_try_leader(void);
void platform_release_leader(void);
Process platform_spawn(char *program, int copy_number);
// Освобождает завершившуюся копию и записывает NO_PROCESS. Возвращает false, если она ещё работает.
bool platform_reap_copy(Process *copy, bool wait);

void platform_start_thread(void (*function)(void));
void platform_join_thread(void);
uint64_t platform_monotonic_ms(void);
void platform_sleep_ms(unsigned milliseconds);
void platform_timestamp(char result[TIMESTAMP_SIZE]);
uint64_t platform_pid(void);

#endif
