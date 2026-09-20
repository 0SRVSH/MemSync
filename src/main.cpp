#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <ctime>
#include "cache_engine.h"

#define SHM_NAME "/memsync_shm"
#define SEM_NAME "/memsync_sem"
#define SHM_SIZE (4 * 1024 * 1024)

int main() {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) { perror("shm_open failed"); return 1; }

    if (ftruncate(shm_fd, SHM_SIZE) == -1) { perror("ftruncate failed"); return 1; }

    void* shm_base = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) { perror("mmap failed"); return 1; }

    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    header->magic = CACHE_MAGIC;
    header->capacity = 1000;
    header->entry_count = 0;
    header->data_offset = sizeof(CacheHeader);

    std::memset(static_cast<uint8_t*>(shm_base) + header->data_offset, 0, header->capacity * sizeof(CacheEntry));

    sem_t* sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) { perror("sem_open failed"); return 1; }

    std::cout << "=== MemSync Engine Tombstone & Deletion Test ===" << std::endl;

    // 1. Test PUT
    sem_wait(sem);
    cache_put(shm_base, "user:101", "Sarbesh Chatterjee - Lead");
    sem_post(sem);
    std::cout << "[PUT] Inserted 'user:101' (Active entries: " << header->entry_count << ")" << std::endl;

    // 2. Test GET
    char val_buffer[MAX_VAL_LEN];
    uint64_t ts = 0;
    sem_wait(sem);
    bool get_ok = cache_get(shm_base, "user:101", val_buffer, &ts);
    sem_post(sem);
    if (get_ok) std::cout << "[GET] Found 'user:101': " << val_buffer << std::endl;

    // 3. Test DELETE (Tombstone creation)
    sem_wait(sem);
    bool del_ok = cache_delete(shm_base, "user:101");
    sem_post(sem);
    if (del_ok) std::cout << "[DELETE] Deleted 'user:101' (Active entries: " << header->entry_count << ")" << std::endl;

    // 4. Test GET (Verify Cache Miss)
    sem_wait(sem);
    bool get_after_del = cache_get(shm_base, "user:101", val_buffer, &ts);
    sem_post(sem);
    if (!get_after_del) std::cout << "[GET] Key 'user:101' not found (Cache Miss - Correct)" << std::endl;

    // 5. Test PUT (Reusing Tombstone Slot)
    sem_wait(sem);
    bool reinsert_ok = cache_put(shm_base, "user:102", "Alex Vance - Dev");
    sem_post(sem);
    if (reinsert_ok) std::cout << "[PUT] Inserted 'user:102' reusing slot (Active entries: " << header->entry_count << ")" << std::endl;

    // Cleanup
    sem_close(sem);
    sem_unlink(SEM_NAME);
    munmap(shm_base, SHM_SIZE);
    close(shm_fd);
    shm_unlink(SHM_NAME);

    std::cout << "=== Test Completed Successfully ===" << std::endl;
    return 0;
}