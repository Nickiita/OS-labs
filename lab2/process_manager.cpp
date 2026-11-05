#include "process_manager.h"
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>

ProcessManager::ProcessHandle ProcessManager::start_process(const std::string& path, const std::vector<std::string>& args) {
    pid_t pid = fork();

    if (pid < 0) {
        return -1; // Ошибка создания процесса
    } else if (pid == 0) {
        // Дочерний процесс
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(path.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(path.c_str(), argv.data());
        std::perror("execvp failed");
        _exit(127);
    }

    // Родительский процесс возвращает PID
    return pid;
}

int ProcessManager::wait_for_process(ProcessHandle handle) {
    if (handle < 0) return -1;

    int status = 0;
    pid_t result = waitpid(handle, &status, 0);

    if (result < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return -128 - WTERMSIG(status);
    }

    return -1;
}