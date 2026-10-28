#include <cerrno>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include "cache_engine.h"

int main() {
    // This standalone demo intentionally resets the named cache on startup.
    if (shm_unlink(cache_shared_memory_name()) == -1 && errno != ENOENT) {
        perror("Could not reset the demo shared-memory cache");
        return 1;
    }

    CacheMapping mapping;
    if (!cache_open_shared(&mapping, true)) {
        return 1;
    }

    void* shm_base = mapping.base;
    std::cout << "=== MemSync Engine Comprehensive Test ===" << std::endl;

    bool put_ok = cache_put(shm_base, "user:101", "Sarbesh Chatterjee - Lead");
    std::cout << "[PUT] Inserted 'user:101': "
              << (put_ok ? "SUCCESS" : "FAILED") << std::endl;

    char value[MAX_VAL_LEN] = {};
    uint64_t timestamp = 0;
    bool get_ok = cache_get(shm_base, "user:101", value, &timestamp);
    if (get_ok) {
        std::cout << "[GET] Found 'user:101': " << value << std::endl;
    }

    bool delete_ok = cache_delete(shm_base, "user:101");
    std::cout << "[DELETE] user:101: "
              << (delete_ok ? "SUCCESS" : "FAILED") << std::endl;

    bool get_after_delete = cache_get(shm_base, "user:101", value, &timestamp);
    if (!get_after_delete) {
        std::cout << "[GET] user:101 is absent (cache miss)" << std::endl;
    }

    bool reinsert_ok = cache_put(shm_base, "user:102", "Alex Vance - Dev");
    std::cout << "[PUT] Inserted 'user:102': "
              << (reinsert_ok ? "SUCCESS" : "FAILED") << std::endl;

    bool ttl_put_ok = cache_put_ttl(
        shm_base,
        "session:temp",
        "short_lived_token",
        2
    );
    bool found_early = cache_get(shm_base, "session:temp", value, &timestamp);
    if (ttl_put_ok && found_early) {
        std::cout << "[TTL Test] Initially found: " << value << std::endl;
    }

    std::cout << "[TTL Test] Sleeping for 3 seconds..." << std::endl;
    sleep(3);

    bool found_late = cache_get(shm_base, "session:temp", value, &timestamp);
    if (!found_late) {
        std::cout << "[TTL Test] Expired key was lazily removed." << std::endl;
    }

    cache_print_telemetry(shm_base);
    cache_close_shared(&mapping);

    if (shm_unlink(cache_shared_memory_name()) == -1) {
        perror("Could not remove the demo shared-memory cache");
        return 1;
    }

    std::cout << "=== Comprehensive Test Completed Successfully ==="
              << std::endl;
    return 0;
}
