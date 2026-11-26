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

extern int64_t *counter;
extern FILE *log_file;

void platform_open(void);
void platform_close(void);
void platform_lock(void);
void platform_unlock(void);
bool try_leader(void);
void release_leader(void);
Process spawn(char *program, int number);
bool reap(Process *child, bool wait);
void start_thread(void (*function)(void));
void join_thread(void);
uint64_t monotonic_ms(void);
void sleep_ms(unsigned ms);
void timestamp(char result[32]);
unsigned long process_id(void);

#endif
