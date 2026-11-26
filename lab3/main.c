#include "platform.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    TIMER_STEP_MS = 100,
    TICK_MS = 300,
    LOG_MS = 1000,
    SPAWN_MS = 3000,
    CHILD_DELAY_MS = 2000
};

static atomic_bool stop = false;
static char *program;

// Called with the data lock held.
static void log_message(const char *message, int copy) {
    char date[32];
    timestamp(date);
    if (copy)
        fprintf(log_file, "%s PID=%lu copy=%d %s\n", date, process_id(), copy, message);
    else
        fprintf(log_file, "%s PID=%lu %s\n", date, process_id(), message);
    if (fflush(log_file) == EOF || ferror(log_file)) {
        fputs("Cannot write lab3.log\n", stderr);
        exit(EXIT_FAILURE);
    }
}

// Avoid signed integer overflow (amount is always positive).
static void add(int amount) {
    *counter = *counter > INT64_MAX - amount ? INT64_MAX : *counter + amount;
}

static void run_copy(int number) {
    platform_lock();
    log_message("started", number);
    if (number == 1) {
        add(10);
    } else if (*counter > INT64_MAX / 2) {
        *counter = INT64_MAX;
    } else if (*counter < INT64_MIN / 2) {
        *counter = INT64_MIN;
    } else {
        *counter *= 2;
    }
    platform_unlock();

    if (number == 2) sleep_ms(CHILD_DELAY_MS);

    platform_lock();
    if (number == 2) *counter /= 2;
    log_message("exited", number);
    platform_unlock();
}

static void start_copies(Process children[2]) {
    for (int i = 0; i < 2; ++i) {
        if (!reap(&children[i], false)) {
            platform_lock();
            log_message("still running, skipped", i + 1);
            platform_unlock();
            continue;
        }
        children[i] = spawn(program, i + 1);
        if (!children[i]) {
            platform_lock();
            log_message("failed to start", i + 1);
            platform_unlock();
        }
    }
}

static void timers(void) {
    Process children[2] = {0, 0};
    bool leader = false;
    int ticks = 0, leader_ticks = 0;
    uint64_t next = monotonic_ms();

    while (!atomic_load(&stop)) {
        if (!leader) leader = try_leader();
        next += TIMER_STEP_MS;
        uint64_t now = monotonic_ms();
        if (next > now) sleep_ms((unsigned)(next - now));
        else next = now;
        if (atomic_load(&stop)) break;

        if (++ticks == TICK_MS / TIMER_STEP_MS) {
            platform_lock();
            add(1);
            platform_unlock();
            ticks = 0;
        }
        if (!leader) continue;

        if (++leader_ticks % (LOG_MS / TIMER_STEP_MS) == 0) {
            char message[64];
            platform_lock();
            snprintf(message, sizeof(message), "counter=%" PRId64, *counter);
            log_message(message, 0);
            platform_unlock();
        }
        if (leader_ticks == SPAWN_MS / TIMER_STEP_MS) {
            start_copies(children);
            leader_ticks = 0;
        }
    }
    if (leader) {
        for (int i = 0; i < 2; ++i) reap(&children[i], true);
        release_leader();
    }
}

static void commands(void) {
    char line[128];
    puts("Commands: set <integer>, quit");
    fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        if (!strchr(line, '\n') && !feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) { }
            puts("Command is too long");
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "quit") == 0) break;
        if (strncmp(line, "set ", 4) != 0) {
            puts("Use set <integer> or quit");
            continue;
        }
        char *end;
        errno = 0;
        intmax_t value = strtoimax(line + 4, &end, 10);
        if (end == line + 4 || errno == ERANGE || value < INT64_MIN || value > INT64_MAX) {
            puts("Invalid integer");
            continue;
        }
        while (isspace((unsigned char)*end)) ++end;
        if (*end != '\0') {
            puts("Invalid integer");
            continue;
        }
        platform_lock();
        *counter = (int64_t)value;
        platform_unlock();
        printf("Counter set to %" PRId64 "\n", (int64_t)value);
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    int copy = 0;
    if (argc == 2 && strcmp(argv[1], "--child1") == 0) copy = 1;
    else if (argc == 2 && strcmp(argv[1], "--child2") == 0) copy = 2;
    else if (argc != 1) return EXIT_FAILURE;

    platform_open();
    if (copy) {
        run_copy(copy);
    } else {
        platform_lock();
        log_message("started", 0);
        platform_unlock();
        program = argv[0];
        start_thread(timers);
        commands();
        atomic_store(&stop, true);
        join_thread();
    }
    platform_close();
    return EXIT_SUCCESS;
}
