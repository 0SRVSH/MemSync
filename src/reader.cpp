#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include "cache_engine.h"

#define SHM_NAME "/memsync_shm"
#define SEM_NAME "/memsync_sem"
#define SHM_SIZE (4 * 1024 * 1024)

int main() {
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (shm_fd == -1) {
        perror("reader: shm_open failed (start writer first)");
        return 1;
    }

    void* shm_base = mmap(nullptr, SHM_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) { perror("reader: mmap failed"); return 1; }

    sem_t* sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("reader: sem_open failed");
        munmap(shm_base, SHM_SIZE);
        close(shm_fd);
        return 1;
    }

    const char* keys[] = {"db:user:1", "db:user:2", "db:status"};
    char val_buf[MAX_VAL_LEN];
    uint64_t ts = 0;

    std::cout << "[Reader] Polling shared memory (zero-copy IPC)..." << std::endl;
    for (int loop = 0; loop < 5; loop++) {
        std::cout << "--- Tick " << loop + 1 << " ---" << std::endl;
        sem_wait(sem);
        for (int i = 0; i < 3; i++) {
            bool found = cache_get(shm_base, keys[i], val_buf, &ts);
            if (found) {
                std::cout << "[Reader] GET " << keys[i] << " = '" << val_buf << "' (ts=" << ts << ")" << std::endl;
            } else {
                std::cout << "[Reader] GET " << keys[i] << " -> MISS" << std::endl;
            }
        }
        sem_post(sem);
        sleep(2);
    }

    sem_close(sem);
    munmap(shm_base, SHM_SIZE);
    close(shm_fd);
    std::cout << "[Reader] Done." << std::endl;
    return 0;
}