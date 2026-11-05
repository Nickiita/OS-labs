#pragma once

#include <string>
#include <vector>
#include <sys/types.h>

class ProcessManager {
public:
    using ProcessHandle = pid_t;

    // Запуск программы в фоновом режиме
    static ProcessHandle start_process(const std::string& path, const std::vector<std::string>& args);

    // Ожидание завершения и получение кода возврата
    static int wait_for_process(ProcessHandle handle);
};