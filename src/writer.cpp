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
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) { perror("writer: shm_open failed"); return 1; }

    if (ftruncate(shm_fd, SHM_SIZE) == -1) { perror("writer: ftruncate failed"); return 1; }

    void* shm_base = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) { perror("writer: mmap failed"); return 1; }

    sem_t* sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) { perror("writer: sem_open failed"); return 1; }

    CacheHeader* h = static_cast<CacheHeader*>(shm_base);
    
    // Self-healing initialization if shared RAM is fresh
    sem_wait(sem);
    if (h->magic != CACHE_MAGIC) {
        h->magic = CACHE_MAGIC;
        h->capacity = 1000;
        h->entry_count = 0;
        h->data_offset = sizeof(CacheHeader);
        std::memset(static_cast<uint8_t*>(shm_base) + h->data_offset, 0, h->capacity * sizeof(CacheEntry));
        std::cout << "[Writer] Initialized shared memory table." << std::endl;
    }
    sem_post(sem);

    std::cout << "[Writer] Attached. Capacity: " << h->capacity << std::endl;

    const char* keys[] = {"db:user:1", "db:user:2", "db:status"};
    const char* vals[] = {"Alice_Admin", "Bob_Operator", "ACTIVE"};

    for (int i = 0; i < 3; i++) {
        sem_wait(sem);
        bool ok = cache_put(shm_base, keys[i], vals[i]);
        sem_post(sem);
        std::cout << "[Writer] PUT " << keys[i] << " -> " << (ok ? "SUCCESS" : "FAILED") 
                  << " | Active count: " << h->entry_count << std::endl;
        sleep(2);
    }

    sem_close(sem);
    munmap(shm_base, SHM_SIZE);
    close(shm_fd);
    std::cout << "[Writer] Done." << std::endl;
    return 0;
}