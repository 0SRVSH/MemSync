#include <iostream>
#include <unistd.h>
#include "cache_engine.h"

int main() {
    CacheMapping mapping;
    if (!cache_open_shared(&mapping, false)) {
        std::cerr << "[Reader] Start the writer or initialize the cache first.\n";
        return 1;
    }

    const char* keys[] = {"db:user:1", "db:user:2", "db:status"};
    char value[MAX_VAL_LEN];
    uint64_t timestamp = 0;

    std::cout << "[Reader] Polling shared memory using the process-shared "
                 "reader-writer lock..." << std::endl;

    for (int loop = 0; loop < 5; ++loop) {
        std::cout << "\n--- Tick " << loop + 1 << " ---" << std::endl;

        for (int i = 0; i < 3; ++i) {
            bool found = cache_get(mapping.base, keys[i], value, &timestamp);
            if (found) {
                std::cout << "[Reader] GET " << keys[i] << " = '" << value
                          << "' (ts=" << timestamp << ")" << std::endl;
            } else {
                std::cout << "[Reader] GET " << keys[i] << " -> MISS" << std::endl;
            }
        }

        cache_print_telemetry(mapping.base);
        sleep(2);
    }

    cache_close_shared(&mapping);
    std::cout << "[Reader] Done." << std::endl;
    return 0;
}
