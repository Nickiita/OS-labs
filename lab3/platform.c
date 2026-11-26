#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"
#include <stdlib.h>

int64_t *counter;
FILE *log_file;
static void (*thread_function)(void);

// Bytes 0..7 contain the counter. The other offsets identify locks.
enum { DATA = sizeof(int64_t), LEADER, LIVE };

static void fail(const char *message) {
    fprintf(stderr, "%s failed\n", message);
    exit(EXIT_FAILURE);
}

#ifdef _WIN32
#include <process.h>
#include <share.h>

static HANDLE state, mapping, thread;
static CRITICAL_SECTION local_mutex;

static bool lock(int byte, bool wait, bool exclusive) {
    OVERLAPPED pos = {0};
    pos.Offset = byte;
    DWORD flags = (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0) |
                  (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY);
    if (LockFileEx(state, flags, 0, 1, 0, &pos)) return true;
    if (!wait && GetLastError() == ERROR_LOCK_VIOLATION) return false;
    fail("LockFileEx");
    return false;
}

static void unlock(int byte) {
    OVERLAPPED pos = {0};
    pos.Offset = byte;
    if (!UnlockFileEx(state, 0, 1, 0, &pos)) fail("UnlockFileEx");
}

static void open_state(void) {
    state = CreateFileW(L"lab3.state", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (state == INVALID_HANDLE_VALUE) fail("CreateFile");
    lock(DATA, true, true);
    mapping = CreateFileMappingW(state, NULL, PAGE_READWRITE, 0, sizeof(*counter), NULL);
    if (!mapping) fail("CreateFileMapping");
    counter = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(*counter));
    if (!counter) fail("MapViewOfFile");
    log_file = _fsopen("lab3.log", "ab", _SH_DENYNO);
    InitializeCriticalSection(&local_mutex);
}

static void close_state(void) {
    DeleteCriticalSection(&local_mutex);
    UnmapViewOfFile(counter);
    CloseHandle(mapping);
    CloseHandle(state);
}

void platform_lock(void) {
    EnterCriticalSection(&local_mutex);
    lock(DATA, true, true);
}

void platform_unlock(void) {
    unlock(DATA);
    LeaveCriticalSection(&local_mutex);
}

Process spawn(char *program, int number) {
    (void)program;
    wchar_t path[32768];
    wchar_t child1[] = L"lab3 --child1", child2[] = L"lab3 --child2";
    wchar_t *command = number == 1 ? child1 : child2;
    DWORD length = GetModuleFileNameW(NULL, path, 32768);
    if (!length || length >= 32768) return 0;
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION child;
    startup.cb = sizeof(startup);
    if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0,
                        NULL, NULL, &startup, &child)) return 0;
    CloseHandle(child.hThread);
    return child.hProcess;
}

bool reap(Process *child, bool wait) {
    if (!*child) return true;
    DWORD result = WaitForSingleObject(*child, wait ? INFINITE : 0);
    if (result == WAIT_TIMEOUT) return false;
    if (result != WAIT_OBJECT_0) fail("WaitForSingleObject");
    CloseHandle(*child);
    *child = 0;
    return true;
}

static unsigned __stdcall thread_main(void *arg) {
    (void)arg;
    thread_function();
    return 0;
}

void start_thread(void (*function)(void)) {
    thread_function = function;
    thread = (HANDLE)_beginthreadex(NULL, 0, thread_main, NULL, 0, NULL);
    if (!thread) fail("_beginthreadex");
}

void join_thread(void) {
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) fail("joining thread");
    CloseHandle(thread);
}

uint64_t monotonic_ms(void) { return GetTickCount64(); }
void sleep_ms(unsigned ms) { Sleep(ms); }
unsigned long process_id(void) { return GetCurrentProcessId(); }

void timestamp(char result[32]) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(result, 32, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
             (unsigned)t.wYear, (unsigned)t.wMonth, (unsigned)t.wDay,
             (unsigned)t.wHour, (unsigned)t.wMinute, (unsigned)t.wSecond,
             (unsigned)t.wMilliseconds);
}

#else
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
static int state;
static pthread_t thread;
static pthread_mutex_t local_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool lock(int byte, bool wait, bool exclusive) {
    struct flock f = {0};
    f.l_type = exclusive ? F_WRLCK : F_RDLCK;
    f.l_whence = SEEK_SET;
    f.l_start = byte;
    f.l_len = 1;
    int result;
    do { result = fcntl(state, wait ? F_SETLKW : F_SETLK, &f); }
    while (result == -1 && errno == EINTR);
    if (result == 0) return true;
    if (!wait && (errno == EACCES || errno == EAGAIN)) return false;
    fail("fcntl lock");
    return false;
}

static void unlock(int byte) {
    struct flock f = {0};
    f.l_type = F_UNLCK;
    f.l_whence = SEEK_SET;
    f.l_start = byte;
    f.l_len = 1;
    if (fcntl(state, F_SETLK, &f) == -1) fail("fcntl unlock");
}

static void open_state(void) {
    state = open("lab3.state", O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (state == -1) fail("open lab3.state");
    lock(DATA, true, true);
    if (ftruncate(state, sizeof(*counter)) == -1) fail("ftruncate");
    counter = mmap(NULL, sizeof(*counter), PROT_READ | PROT_WRITE, MAP_SHARED, state, 0);
    if (counter == MAP_FAILED) fail("mmap");
    log_file = fopen("lab3.log", "a");
    if (log_file && fcntl(fileno(log_file), F_SETFD, FD_CLOEXEC) == -1)
        fail("fcntl FD_CLOEXEC");
}

static void close_state(void) {
    munmap(counter, sizeof(*counter));
    close(state);
    pthread_mutex_destroy(&local_mutex);
}

void platform_lock(void) {
    if (pthread_mutex_lock(&local_mutex) != 0) fail("pthread_mutex_lock");
    lock(DATA, true, true);
}

void platform_unlock(void) {
    unlock(DATA);
    if (pthread_mutex_unlock(&local_mutex) != 0) fail("pthread_mutex_unlock");
}

Process spawn(char *program, int number) {
    char *args[] = {program, number == 1 ? "--child1" : "--child2", NULL};
    Process child;
    return posix_spawnp(&child, program, NULL, NULL, args, environ) == 0 ? child : 0;
}

bool reap(Process *child, bool wait) {
    if (!*child) return true;
    pid_t result;
    do { result = waitpid(*child, NULL, wait ? 0 : WNOHANG); }
    while (result == -1 && errno == EINTR);
    if (result == -1) fail("waitpid");
    if (result == 0) return false;
    *child = 0;
    return true;
}

static void *thread_main(void *arg) {
    (void)arg;
    thread_function();
    return NULL;
}

void start_thread(void (*function)(void)) {
    thread_function = function;
    if (pthread_create(&thread, NULL, thread_main, NULL) != 0) fail("pthread_create");
}

void join_thread(void) {
    if (pthread_join(thread, NULL) != 0) fail("pthread_join");
}

uint64_t monotonic_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) == -1) fail("clock_gettime");
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void sleep_ms(unsigned ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
    while (nanosleep(&t, &t) == -1)
        if (errno != EINTR) fail("nanosleep");
}

unsigned long process_id(void) { return (unsigned long)getpid(); }

void timestamp(char result[32]) {
    struct timespec t;
    struct tm local;
    if (clock_gettime(CLOCK_REALTIME, &t) == -1 || !localtime_r(&t.tv_sec, &local))
        fail("local time");
    size_t n = strftime(result, 32, "%Y-%m-%d %H:%M:%S", &local);
    if (!n) fail("strftime");
    snprintf(result + n, 32 - n, ".%03d", (int)(t.tv_nsec / 1000000));
}
#endif

void platform_open(void) {
    open_state(); // DATA is locked until initialization is complete.
    if (!log_file) fail("open lab3.log");
    if (lock(LIVE, false, true)) {
        *counter = 0; // No other instances are using the counter.
        unlock(LIVE);
    }
    lock(LIVE, true, false);
    unlock(DATA);
}

void platform_close(void) {
    fclose(log_file);
    unlock(LIVE);
    close_state();
}

bool try_leader(void) { return lock(LEADER, false, true); }
void release_leader(void) { unlock(LEADER); }
