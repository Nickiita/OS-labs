#include <iostream>
#include <vector>
#include "process_manager.h"

int main() {
    std::cout << "Starting background process (sleep 2)...\n";

    std::string path = "/bin/sleep";
    std::vector<std::string> args = {"2"};

    auto handle = ProcessManager::start_process(path, args);
    if (handle < 0) {
        std::cerr << "Error: failed to start process\n";
        return 1;
    }

    std::cout << "Process started (PID: " << handle << "). Waiting for completion...\n";
    int exit_code = ProcessManager::wait_for_process(handle);

    std::cout << "Process exited with code: " << exit_code << "\n";
    return 0;
}