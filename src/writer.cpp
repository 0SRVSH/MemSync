#include <iostream>
#include <unistd.h>
#include "cache_engine.h"
#include "mysql_logger.h" // Includes our MySQL logger header

int main() {
    // 1. Open/Initialize POSIX shared memory cache
    CacheMapping mapping;
    if (!cache_open_shared(&mapping, true)) {
        std::cerr << "[Writer] Failed to open shared memory cache.\n";
        return 1;
    }

    CacheHeader* header = static_cast<CacheHeader*>(mapping.base);
    std::cout << "[Writer] " << (mapping.created ? "Initialized" : "Attached to")
              << " shared cache. Capacity: " << header->capacity << std::endl;

    // 2. Initialize MySQL Database Logger connection
    // Parameters: host, user, password, database_name
    // (Make sure to update credentials to match your local MySQL setup)
    MySQLLogger db_logger("localhost", "memsync_user", "memsync_pass", "memsync_db");
    // Get current OS Process ID (OS concept!)
    pid_t current_pid = getpid();
    std::string process_name = "memsync_writer";

    const char* keys[] = {"db:user:1", "db:user:2", "db:status"};
    const char* values[] = {"Alice_Admin", "Bob_Operator", "ACTIVE"};

    for (int i = 0; i < 3; ++i) {
        // --- PATH A: NoSQL Fast Path (Shared Memory In-Memory Cache) ---
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

        // --- PATH B: Relational DBMS Path (MySQL Audit Log via Stored Procedure & Transaction) ---
        // Simulating process telemetry metrics
        double mock_cpu_usage = 10.5 + (i * 4.2);
        int mock_memory_kb = 4096 + (i * 512);

        bool db_logged = db_logger.logProcessViaStoredProcedure(
            static_cast<int>(current_pid),
            process_name,
            "simik",
            "RUNNING",
            mock_cpu_usage,
            mock_memory_kb
        );

        if (db_logged) {
            std::cout << "[MySQL Audit] Logged PID " << current_pid 
                      << " metrics (CPU: " << mock_cpu_usage << "%, Mem: " << mock_memory_kb << "KB) to MySQL.\n";
        }

        sleep(2);
    }

    cache_close_shared(&mapping);
    std::cout << "[Writer] Done." << std::endl;
    return 0;
}