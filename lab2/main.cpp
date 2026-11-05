#include <iostream>
#include <vector>
#include "process_manager.h"

int main() {
    std::cout << "Starting background process...\n";

#ifdef _WIN32
    std::string path = "cmd.exe";
    std::vector<std::string> args = {"/c", "timeout /t 2 >nul"};
#else
    std::string path = "/bin/sleep";
    std::vector<std::string> args = {"2"};
#endif

    auto handle = ProcessManager::start_process(path, args);

#ifdef _WIN32
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: failed to start process!\n";
        return 1;
    }
#else
    if (handle < 0) {
        std::cerr << "Error: failed to start process!\n";
        return 1;
    }
#endif

    std::cout << "Process successfully spawned. Waiting for completion...\n";
    int exit_code = ProcessManager::wait_for_process(handle);

    std::cout << "Process finished with exit code: " << exit_code << "\n";
    return 0;
}