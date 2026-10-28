#include <cerrno>
#include <iostream>
#include <sys/mman.h>
#include "cache_engine.h"

int main() {
    std::cout << "[OS Cleanup] Removing the named MemSync shared-memory cache...\n";
    if (shm_unlink(cache_shared_memory_name()) == 0) {
        std::cout << "[SUCCESS] Unlinked shared memory: "
                  << cache_shared_memory_name() << "\n";
        return 0;
    }
    if (errno == ENOENT) {
        std::cout << "[INFO] No shared-memory cache exists.\n";
        return 0;
    }
    perror("Could not unlink shared-memory cache");
    return 1;
}
