#include "cache_engine.h"
#include "mysql_logger.h"
#include <iostream>
#include <cstring>
#include <unistd.h> // For getpid()

int main() {
    // 1. Open or map the POSIX shared memory region
    CacheMapping mapping;
    // Try to open existing shared memory; create if missing
    if (!cache_open_shared(&mapping, true)) {
        std::cerr << "[Writer] Failed to open shared memory region." << std::endl;
        return 1;
    }

    // Validate the memory layout and header
    if (!cache_validate_layout(mapping.base, mapping.size)) {
        std::cerr << "[Writer] Shared memory layout validation failed." << std::endl;
        cache_close_shared(&mapping);
        return 1;
    }

    // Cast base pointer to header to read capacity
    CacheHeader* header = static_cast<CacheHeader*>(mapping.base);
    std::cout << "[Writer] Attached to shared cache. Capacity: " << header->capacity << std::endl;

    // 2. Initialize the MySQL Relational Telemetry Logger
    MySQLLogger db_logger("localhost", "memsync_user", "memsync_pass", "memsync_db");

    // Get the actual operating system Process ID (PID)
    int current_pid = static_cast<int>(getpid());

    // 3. Perform NoSQL Shared Memory Puts and log MySQL Relational Telemetry
    if (cache_put(mapping.base, "db:user:1", "Alice")) {
        std::cout << "[Writer] PUT db:user:1 -> SUCCESS | Active count: " << header->entry_count << std::endl;
        db_logger.logProcessViaStoredProcedure(current_pid, "memsync_writer", "simik", "RUNNING", 10.5, 4096);
        std::cout << "[MySQL Audit] Logged PID " << current_pid << " metrics (CPU: 10.5%, Mem: 4096KB) to MySQL." << std::endl;
    }

    if (cache_put(mapping.base, "db:user:2", "Bob")) {
        std::cout << "[Writer] PUT db:user:2 -> SUCCESS | Active count: " << header->entry_count << std::endl;
        db_logger.logProcessViaStoredProcedure(current_pid, "memsync_writer", "simik", "RUNNING", 14.7, 4608);
        std::cout << "[MySQL Audit] Logged PID " << current_pid << " metrics (CPU: 14.7%, Mem: 4608KB) to MySQL." << std::endl;
    }

    if (cache_put(mapping.base, "db:status", "Active")) {
        std::cout << "[Writer] PUT db:status -> SUCCESS | Active count: " << header->entry_count << std::endl;
        db_logger.logProcessViaStoredProcedure(current_pid, "memsync_writer", "simik", "RUNNING", 18.9, 5120);
        std::cout << "[MySQL Audit] Logged PID " << current_pid << " metrics (CPU: 18.9%, Mem: 5120KB) to MySQL." << std::endl;
    }

    // Clean up shared memory mapping
    cache_close_shared(&mapping);

    std::cout << "[Writer] Done." << std::endl;
    return 0;
}