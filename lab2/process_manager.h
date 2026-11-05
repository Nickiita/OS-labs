#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

class ProcessManager {
public:
#ifdef _WIN32
    using ProcessHandle = HANDLE;
#else
    using ProcessHandle = pid_t;
#endif

    // Запуск программы в фоновом режиме
    static ProcessHandle start_process(const std::string& path, const std::vector<std::string>& args);

    // Ожидание завершения и получение кода возврата
    static int wait_for_process(ProcessHandle handle);
};