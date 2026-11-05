#include "process_manager.h"
#include <iostream>

#ifdef _WIN32
// Windows Implementation (Win32 API)
ProcessManager::ProcessHandle ProcessManager::start_process(const std::string& path, const std::vector<std::string>& args) {
    // Формируем командную строку: "path" arg1 arg2 ...
    std::string command_line = "\"" + path + "\"";
    for (const auto& arg : args) {
        command_line += " " + arg;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Win32 ожидает неконстантный буфер для командной строки
    std::vector<char> cmd_editable(command_line.begin(), command_line.end());
    cmd_editable.push_back('\0');

    BOOL success = CreateProcessA(
        nullptr,
        cmd_editable.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!success) {
        return nullptr;
    }

    // Закрываем дескриптор потока сразу, так как следим только за процессом
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

int ProcessManager::wait_for_process(ProcessHandle handle) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    // Ожидание завершения процесса
    WaitForSingleObject(handle, INFINITE);

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(handle, &exit_code)) {
        CloseHandle(handle);
        return -1;
    }

    CloseHandle(handle);
    return static_cast<int>(exit_code);
}

#else
// --- UNIX / Linux Implementation (POSIX) ---
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

ProcessManager::ProcessHandle ProcessManager::start_process(const std::string& path, const std::vector<std::string>& args) {
    pid_t pid = fork();

    if (pid < 0) {
        return -1;
    } else if (pid == 0) {
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
#endif