#include "platform.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    TICK_MS = 300,
    LOG_MS = 1000,
    SPAWN_MS = 3000,
    CHILD_DELAY_MS = 2000
};

static SharedState shared;
static atomic_bool stop_requested;
static char *program_path;

static void write_log_locked(const char *format, ...) {
    char timestamp[TIMESTAMP_SIZE];
    va_list args;

    platform_timestamp(timestamp);
    int prefix = fprintf(shared.log, "%s PID=%" PRIu64 " ", timestamp, platform_pid());
    va_start(args, format);
    int body = vfprintf(shared.log, format, args);
    va_end(args);
    int newline = fputc('\n', shared.log);
    int flush = fflush(shared.log);
    if (prefix < 0 || body < 0 || newline == EOF || flush == EOF) {
        fputs("lab3: cannot write to lab3.log\n", stderr);
        exit(EXIT_FAILURE);
    }
}

static void log_copy(int copy_number, const char *event) {
    platform_lock();
    write_log_locked("copy=%d %s", copy_number, event);
    platform_unlock();
}

static void add_to_counter(int64_t amount) {
    platform_lock();
    // При переполнении результат ограничивается ближайшей границей int64_t.
    if (*shared.counter > INT64_MAX - amount) {
        *shared.counter = INT64_MAX;
    } else {
        *shared.counter += amount;
    }
    platform_unlock();
}

static void run_copy(int copy_number) {
    log_copy(copy_number, "started");
    if (copy_number == 1) {
        add_to_counter(10);
    } else {
        platform_lock();
        if (*shared.counter > INT64_MAX / 2) {
            *shared.counter = INT64_MAX;
        } else if (*shared.counter < INT64_MIN / 2) {
            *shared.counter = INT64_MIN;
        } else {
            *shared.counter *= 2;
        }
        platform_unlock();

        platform_sleep_ms(CHILD_DELAY_MS);

        platform_lock();
        *shared.counter /= 2;
        platform_unlock();
    }
    log_copy(copy_number, "exited");
}

static void start_copies(Process copies[2]) {
    for (int i = 0; i < 2; ++i) {
        if (!platform_reap_copy(&copies[i], false)) {
            log_copy(i + 1, "still running, skipped");
            continue;
        }
        copies[i] = platform_spawn(program_path, i + 1);
        if (copies[i] == NO_PROCESS) {
            log_copy(i + 1, "failed to start");
        }
    }
}

static uint64_t next_deadline(uint64_t previous, uint64_t now, unsigned interval) {
    uint64_t next = previous + interval;
    return next > now ? next : now + interval;
}

static void run_timers(void) {
    bool leader = false;
    Process copies[2] = {NO_PROCESS, NO_PROCESS};
    uint64_t next_tick = platform_monotonic_ms() + TICK_MS;
    uint64_t next_log = 0;
    uint64_t next_spawn = 0;

    while (!atomic_load(&stop_requested)) {
        uint64_t now = platform_monotonic_ms();
        if (!leader && platform_try_leader()) {
            leader = true;
            next_log = now + LOG_MS;
            next_spawn = now + SPAWN_MS;
        }

        if (now >= next_tick) {
            add_to_counter(1);
            next_tick = next_deadline(next_tick, now, TICK_MS);
        }

        uint64_t deadline = next_tick;
        if (leader) {
            if (now >= next_log) {
                platform_lock();
                write_log_locked("counter=%" PRId64, *shared.counter);
                platform_unlock();
                next_log = next_deadline(next_log, now, LOG_MS);
            }
            if (now >= next_spawn) {
                start_copies(copies);
                next_spawn = next_deadline(next_spawn, now, SPAWN_MS);
            }
            if (next_log < deadline) {
                deadline = next_log;
            }
            if (next_spawn < deadline) {
                deadline = next_spawn;
            }
        }

        now = platform_monotonic_ms();
        if (deadline > now) {
            platform_sleep_ms((unsigned)(deadline - now));
        }
    }

    if (leader) {
        for (int i = 0; i < 2; ++i) {
            platform_reap_copy(&copies[i], true);
        }
        platform_release_leader();
    }
}

static void read_commands(void) {
    char line[128];

    puts("Commands: set <integer>, quit");
    fflush(stdout);
    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int character;
            do {
                character = getchar();
            } while (character != '\n' && character != EOF);
            fputs("Command is too long.\n", stderr);
            continue;
        }
        while (length > 0 && isspace((unsigned char)line[length - 1])) {
            line[--length] = '\0';
        }
        if (strcmp(line, "quit") == 0) {
            return;
        }
        if (length == 0) {
            continue;
        }
        if (strncmp(line, "set", 3) != 0 || !isspace((unsigned char)line[3])) {
            fputs("Use set <integer> or quit.\n", stderr);
            continue;
        }

        char *end;
        errno = 0;
        intmax_t value = strtoimax(line + 4, &end, 10);
        if (errno == ERANGE || end == line + 4 || *end != '\0' ||
            value < INT64_MIN || value > INT64_MAX) {
            fputs("Enter an integer in the int64_t range.\n", stderr);
            continue;
        }

        platform_lock();
        *shared.counter = (int64_t)value;
        platform_unlock();
        printf("Counter set to %" PRId64 "\n", (int64_t)value);
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    int copy_number = 0;
    if (argc == 2 && strcmp(argv[1], "--child1") == 0) {
        copy_number = 1;
    } else if (argc == 2 && strcmp(argv[1], "--child2") == 0) {
        copy_number = 2;
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return EXIT_FAILURE;
    }

    shared = platform_open();
    if (copy_number != 0) {
        run_copy(copy_number);
    } else {
        platform_lock();
        write_log_locked("started");
        platform_unlock();
        program_path = argv[0];
        platform_start_thread(run_timers);
        read_commands();
        atomic_store(&stop_requested, true);
        platform_join_thread();
    }
    platform_close();
    return EXIT_SUCCESS;
}
