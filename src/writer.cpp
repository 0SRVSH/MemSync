#include <iostream>
#include <unistd.h>
#include "cache_engine.h"

int main() {
    CacheMapping mapping;
    if (!cache_open_shared(&mapping, true)) {
        return 1;
    }

    CacheHeader* header = static_cast<CacheHeader*>(mapping.base);
    std::cout << "[Writer] " << (mapping.created ? "Initialized" : "Attached to")
              << " shared cache. Capacity: " << header->capacity << std::endl;

    const char* keys[] = {"db:user:1", "db:user:2", "db:status"};
    const char* values[] = {"Alice_Admin", "Bob_Operator", "ACTIVE"};

    for (int i = 0; i < 3; ++i) {
        bool ok = cache_put(mapping.base, keys[i], values[i]);
        if (pthread_rwlock_rdlock(&header->rwlock) != 0) {
            std::cerr << "[Writer] Could not acquire cache read lock.\n";
            cache_close_shared(&mapping);
            return 1;
        }
        uint32_t entry_count = header->entry_count;
        pthread_rwlock_unlock(&header->rwlock);
        std::cout << "[Writer] PUT " << keys[i] << " -> "
                  << (ok ? "SUCCESS" : "FAILED")
                  << " | Active count: " << entry_count << std::endl;
        sleep(2);
    }

    cache_close_shared(&mapping);
    std::cout << "[Writer] Done." << std::endl;
    return 0;
}
