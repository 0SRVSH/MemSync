#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cache_engine.h"

#define SHM_NAME "/memsync_shm"
#define SHM_SIZE (4 * 1024 * 1024)

int main() {
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666); // O_RDWR allows updating telemetry counters
    if (shm_fd == -1) {
        perror("[Reader] shm_open failed (start writer/init first)");
        return 1;
    }

    void* shm_base = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) {
        perror("[Reader] mmap failed");
        close(shm_fd);
        return 1;
    }

    const char* keys[] = {"db:user:1", "db:user:2", "db:status"};
    char val_buf[MAX_VAL_LEN];
    uint64_t ts = 0;

    std::cout << "[Reader] Polling shared memory using concurrent RWLock (zero-copy IPC)..." << std::endl;

    for (int loop = 0; loop < 5; loop++) {
        std::cout << "\n--- Tick " << loop + 1 << " ---" << std::endl;

        for (int i = 0; i < 3; i++) {
            // cache_get internally acquires a shared read lock (pthread_rwlock_rdlock)
            bool found = cache_get(shm_base, keys[i], val_buf, &ts);
            if (found) {
                std::cout << "[Reader] GET " << keys[i] << " = '" << val_buf << "' (ts=" << ts << ")" << std::endl;
            } else {
                std::cout << "[Reader] GET " << keys[i] << " -> MISS" << std::endl;
            }
        }

        // Telemetry acquires a shared read lock internally
        cache_print_telemetry(shm_base);
        sleep(2);
    }

    munmap(shm_base, SHM_SIZE);
    close(shm_fd);
    std::cout << "[Reader] Done." << std::endl;
    return 0;
}