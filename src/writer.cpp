#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cache_engine.h"

#define SHM_NAME "/memsync_shm"
#define SHM_SIZE (4 * 1024 * 1024)

int main() {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[Writer] shm_open failed");
        return 1;
    }

    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("[Writer] ftruncate failed");
        close(shm_fd);
        return 1;
    }

    void* shm_base = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) {
        perror("[Writer] mmap failed");
        close(shm_fd);
        return 1;
    }

    CacheHeader* h = static_cast<CacheHeader*>(shm_base);

    // Initialize shared memory header and process-shared rwlock on first boot
    if (h->magic != CACHE_MAGIC) {
        h->magic = CACHE_MAGIC;
        h->capacity = 1000;
        h->entry_count = 0;
        h->data_offset = sizeof(CacheHeader);
        h->total_hits = 0;
        h->total_misses = 0;
        h->total_collisions = 0;
        
        std::memset(static_cast<uint8_t*>(shm_base) + h->data_offset, 0, h->capacity * sizeof(CacheEntry));
        
        if (!cache_init_lock(h)) {
            std::cerr << "[Writer] Failed to initialize process-shared rwlock!" << std::endl;
            munmap(shm_base, SHM_SIZE);
            close(shm_fd);
            return 1;
        }
        std::cout << "[Writer] Initialized shared memory table and rwlock." << std::endl;
    }

    std::cout << "[Writer] Attached to SHM. Capacity: " << h->capacity << std::endl;

    const char* keys[] = {"db:user:1", "db:user:2", "db:status"};
    const char* vals[] = {"Alice_Admin", "Bob_Operator", "ACTIVE"};

    for (int i = 0; i < 3; i++) {
        // cache_put internally acquires an exclusive write lock (pthread_rwlock_wrlock)
        bool ok = cache_put(shm_base, keys[i], vals[i]);
        std::cout << "[Writer] PUT " << keys[i] << " -> " << (ok ? "SUCCESS" : "FAILED") 
                  << " | Active count: " << h->entry_count << std::endl;
        sleep(2);
    }

    munmap(shm_base, SHM_SIZE);
    close(shm_fd);
    std::cout << "[Writer] Done." << std::endl;
    return 0;
}