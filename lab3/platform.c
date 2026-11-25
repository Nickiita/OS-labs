#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#include <inttypes.h>
#include <stdlib.h>

static SharedState shared;
static void (*thread_function)(void);

static void fail(const char *operation) {
    fprintf(stderr, "lab3: %s failed\n", operation);
    exit(EXIT_FAILURE);
}

#ifdef _WIN32

#include <io.h>
#include <process.h>
#include <share.h>
#include <wchar.h>

static HANDLE mapping;
static HANDLE shared_mutex;
static HANDLE leader_mutex;
static HANDLE timer_thread;

SharedState platform_open(void) {
    shared.log = _fsopen("lab3.log", "a+b", _SH_DENYNO);
    if (shared.log == NULL) {
        fail("opening lab3.log");
    }

    BY_HANDLE_FILE_INFORMATION info;
    HANDLE log_handle = (HANDLE)_get_osfhandle(_fileno(shared.log));
    if (!GetFileInformationByHandle(log_handle, &info)) {
        fail("identifying lab3.log");
    }

    // Имя IPC-объекта одинаково у всех, кто открыл один и тот же лог.
    char name[96];
    int length = snprintf(name, sizeof(name), "Local\\lab3-%08lx-%08lx%08lx-",
                          (unsigned long)info.dwVolumeSerialNumber,
                          (unsigned long)info.nFileIndexHigh,
                          (unsigned long)info.nFileIndexLow);
    char *suffix = name + length;
    size_t available = sizeof(name) - (size_t)length;

    snprintf(suffix, available, "counter");
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, sizeof(*shared.counter), name);
    if (mapping == NULL) {
        fail("creating shared memory");
    }
    shared.counter = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(*shared.counter));
    if (shared.counter == NULL) {
        fail("mapping shared memory");
    }

    snprintf(suffix, available, "data");
    shared_mutex = CreateMutexA(NULL, FALSE, name);
    snprintf(suffix, available, "leader");
    leader_mutex = CreateMutexA(NULL, FALSE, name);
    if (shared_mutex == NULL || leader_mutex == NULL) {
        fail("creating mutexes");
    }
    return shared;
}

void platform_close(void) {
    UnmapViewOfFile(shared.counter);
    CloseHandle(mapping);
    CloseHandle(shared_mutex);
    CloseHandle(leader_mutex);
    fclose(shared.log);
}

void platform_lock(void) {
    DWORD result = WaitForSingleObject(shared_mutex, INFINITE);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
        fail("locking shared data");
    }
}

void platform_unlock(void) {
    if (!ReleaseMutex(shared_mutex)) {
        fail("unlocking shared data");
    }
}

bool platform_try_leader(void) {
    DWORD result = WaitForSingleObject(leader_mutex, 0);
    if (result == WAIT_TIMEOUT) {
        return false;
    }
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
        fail("acquiring leadership");
    }
    return true;
}

void platform_release_leader(void) {
    if (!ReleaseMutex(leader_mutex)) {
        fail("releasing leadership");
    }
}

Process platform_spawn(char *program, int copy_number) {
    (void)program;
    wchar_t executable[32768];
    DWORD length = GetModuleFileNameW(NULL, executable, 32768);
    if (length == 0 || length >= 32768) {
        return NO_PROCESS;
    }

    wchar_t child1_command[] = L"lab3 --child1";
    wchar_t child2_command[] = L"lab3 --child2";
    wchar_t *command = copy_number == 1 ? child1_command : child2_command;

    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process;
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, 0,
                        NULL, NULL, &startup, &process)) {
        return NO_PROCESS;
    }
    CloseHandle(process.hThread);
    return process.hProcess;
}

bool platform_reap_copy(Process *copy, bool wait) {
    if (*copy == NO_PROCESS) {
        return true;
    }
    DWORD result = WaitForSingleObject(*copy, wait ? INFINITE : 0);
    if (result == WAIT_TIMEOUT) {
        return false;
    }
    if (result != WAIT_OBJECT_0) {
        fail("waiting for a copy");
    }
    CloseHandle(*copy);
    *copy = NO_PROCESS;
    return true;
}

static unsigned __stdcall thread_main(void *argument) {
    (void)argument;
    thread_function();
    return 0;
}

void platform_start_thread(void (*function)(void)) {
    thread_function = function;
    timer_thread = (HANDLE)_beginthreadex(NULL, 0, thread_main, NULL, 0, NULL);
    if (timer_thread == NULL) {
        fail("starting timer thread");
    }
}

void platform_join_thread(void) {
    if (WaitForSingleObject(timer_thread, INFINITE) != WAIT_OBJECT_0) {
        fail("waiting for timer thread");
    }
    CloseHandle(timer_thread);
}

uint64_t platform_monotonic_ms(void) {
    return GetTickCount64();
}

void platform_sleep_ms(unsigned milliseconds) {
    Sleep(milliseconds);
}

void platform_timestamp(char result[TIMESTAMP_SIZE]) {
    SYSTEMTIME time;
    GetLocalTime(&time);
    snprintf(result, TIMESTAMP_SIZE, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
             (unsigned)time.wYear, (unsigned)time.wMonth, (unsigned)time.wDay,
             (unsigned)time.wHour, (unsigned)time.wMinute, (unsigned)time.wSecond,
             (unsigned)time.wMilliseconds);
}

uint64_t platform_pid(void) {
    return GetCurrentProcessId();
}

#else

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

// Три независимые блокировки на байтах открытого файла лога.
enum { DATA_BYTE, LEADER_BYTE, LIVE_BYTE };

static int log_fd;
static char shm_name[32];
static pthread_mutex_t local_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timer_thread;

static bool file_lock(off_t byte, short type, int command) {
    struct flock lock = {
        .l_type = type,
        .l_whence = SEEK_SET,
        .l_start = byte,
        .l_len = 1
    };
    int result;
    do {
        result = fcntl(log_fd, command, &lock);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        if (command == F_SETLK && type == F_WRLCK && (errno == EACCES || errno == EAGAIN)) {
            return false;
        }
        fail("locking lab3.log");
    }
    return command != F_GETLK || lock.l_type != F_UNLCK;
}

void platform_lock(void) {
    // fcntl различает процессы, pthread_mutex различает потоки одного процесса.
    if (pthread_mutex_lock(&local_mutex) != 0) {
        fail("locking local mutex");
    }
    file_lock(DATA_BYTE, F_WRLCK, F_SETLKW);
}

void platform_unlock(void) {
    file_lock(DATA_BYTE, F_UNLCK, F_SETLK);
    if (pthread_mutex_unlock(&local_mutex) != 0) {
        fail("unlocking local mutex");
    }
}

SharedState platform_open(void) {
    // Для F_RDLCK на LIVE_BYTE нужен дескриптор, открытый также на чтение.
    shared.log = fopen("lab3.log", "a+");
    if (shared.log == NULL) {
        fail("opening lab3.log");
    }
    log_fd = fileno(shared.log);
    struct stat info;
    if (fcntl(log_fd, F_SETFD, FD_CLOEXEC) == -1 || fstat(log_fd, &info) == -1) {
        fail("identifying lab3.log");
    }
    uint64_t key = ((uint64_t)info.st_dev * UINT64_C(11400714819323198485)) ^ (uint64_t)info.st_ino;
    snprintf(shm_name, sizeof(shm_name), "/lab3-%016" PRIx64, key);

    platform_lock();
    bool first = !file_lock(LIVE_BYTE, F_WRLCK, F_GETLK);
    if (first && shm_unlink(shm_name) == -1 && errno != ENOENT) {
        fail("removing old shared memory");
    }
    int fd = shm_open(shm_name, first ? O_RDWR | O_CREAT | O_EXCL : O_RDWR, 0600);
    if (fd == -1 || (first && ftruncate(fd, sizeof(*shared.counter)) == -1)) {
        fail("opening shared memory");
    }
    shared.counter = mmap(NULL, sizeof(*shared.counter), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shared.counter == MAP_FAILED) {
        fail("mapping shared memory");
    }
    file_lock(LIVE_BYTE, F_RDLCK, F_SETLK);
    platform_unlock();
    return shared;
}

void platform_close(void) {
    platform_lock();
    file_lock(LIVE_BYTE, F_UNLCK, F_SETLK);
    if (!file_lock(LIVE_BYTE, F_WRLCK, F_GETLK) && shm_unlink(shm_name) == -1) {
        fail("removing shared memory");
    }
    platform_unlock();

    munmap(shared.counter, sizeof(*shared.counter));
    fclose(shared.log);
}

bool platform_try_leader(void) {
    return file_lock(LEADER_BYTE, F_WRLCK, F_SETLK);
}

void platform_release_leader(void) {
    file_lock(LEADER_BYTE, F_UNLCK, F_SETLK);
}

Process platform_spawn(char *program, int copy_number) {
    char *args[] = {program, copy_number == 1 ? "--child1" : "--child2", NULL};
    Process process;
    if (posix_spawnp(&process, program, NULL, NULL, args, environ) != 0) {
        return NO_PROCESS;
    }
    return process;
}

bool platform_reap_copy(Process *copy, bool wait) {
    if (*copy == NO_PROCESS) {
        return true;
    }
    Process result;
    do {
        result = waitpid(*copy, NULL, wait ? 0 : WNOHANG);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        fail("waiting for a copy");
    }
    if (result == 0) {
        return false;
    }
    *copy = NO_PROCESS;
    return true;
}

static void *thread_main(void *argument) {
    (void)argument;
    thread_function();
    return NULL;
}

void platform_start_thread(void (*function)(void)) {
    thread_function = function;
    if (pthread_create(&timer_thread, NULL, thread_main, NULL) != 0) {
        fail("starting timer thread");
    }
}

void platform_join_thread(void) {
    if (pthread_join(timer_thread, NULL) != 0) {
        fail("waiting for timer thread");
    }
}

uint64_t platform_monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        fail("reading monotonic time");
    }
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

void platform_sleep_ms(unsigned milliseconds) {
    struct timespec interval = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000
    };
    while (nanosleep(&interval, &interval) == -1) {
        if (errno != EINTR) {
            fail("waiting on timer");
        }
    }
}

void platform_timestamp(char result[TIMESTAMP_SIZE]) {
    struct timespec now;
    struct tm local;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1 || localtime_r(&now.tv_sec, &local) == NULL) {
        fail("reading local time");
    }
    size_t length = strftime(result, TIMESTAMP_SIZE, "%Y-%m-%d %H:%M:%S", &local);
    if (length == 0) {
        fail("formatting local time");
    }
    snprintf(result + length, TIMESTAMP_SIZE - length, ".%03d", (int)(now.tv_nsec / 1000000));
}

uint64_t platform_pid(void) {
    return (uint64_t)getpid();
}

#endif
